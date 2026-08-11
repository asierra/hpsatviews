/* Fast chunked NetCDF-4/HDF5 read: parallel libdeflate decompression.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * GOES ABI stores each variable as a 2-D chunked HDF5 dataset filtered with
 * shuffle + deflate (gzip). HDF5's own read path decompresses every chunk on a
 * single thread behind a global lock — the dominant cost of loading a full-disk
 * scene. Here we read the raw (still-compressed) chunks with H5Dread_chunk and
 * decompress them across all cores with libdeflate (~2x faster per core than
 * zlib), then invert the shuffle and scatter into the destination grid.
 *
 * Anything outside the expected layout falls back (returns non-zero) so the
 * caller re-reads with nc_get_var — correctness never depends on this path.
 */

#include "reader_nc_chunk.h"
#include "logger.h"

#include <fcntl.h>
#include <hdf5.h>
#include <libdeflate.h>
#include <omp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Inverse of the HDF5 shuffle filter: the shuffled layout stores byte-plane 0
 * of every element, then byte-plane 1, ... Rebuild interleaved elements. */
static void unshuffle(const uint8_t *shuf, uint8_t *out, size_t nelem,
                      size_t elem_size) {
  for (size_t b = 0; b < elem_size; b++) {
    const uint8_t *plane = shuf + b * nelem;
    for (size_t i = 0; i < nelem; i++) out[i * elem_size + b] = plane[i];
  }
}

#if H5_VERSION_GE(1, 14, 0)
/* Collects the chunk index in a single walk (see the serial phase below).
 * H5Dchunk_iter only visits *allocated* chunks, so any entry left at rawsize 0
 * is an all-fill region — the same thing the per-chunk lookup reports as
 * HADDR_UNDEF. */
typedef struct {
  hsize_t *rawsize;
  unsigned *fmask;
  haddr_t *rawaddr; /* offset de cada chunk dentro del archivo, para pread() */
  size_t nchx, nchy, chy, chx;
  bool ok;
} ChunkIndex;

static int chunk_index_cb(const hsize_t *offset, unsigned filter_mask,
                          haddr_t addr, hsize_t size, void *op_data) {
  ChunkIndex *idx = (ChunkIndex *)op_data;
  size_t cy = (size_t)offset[0] / idx->chy;
  size_t cx = (size_t)offset[1] / idx->chx;
  if (cy >= idx->nchy || cx >= idx->nchx) { /* offset outside the grid we sized */
    idx->ok = false;
    return -1;
  }
  size_t k = cy * idx->nchx + cx;
  idx->rawsize[k] = size;
  idx->fmask[k] = filter_mask;
  idx->rawaddr[k] = addr;
  return 0;
}
#endif

int read_var_chunked_deflate(const char *filename, const char *varname,
                             void *out, size_t nx, size_t ny,
                             size_t elem_size) {
  if (elem_size != 2) return 1; /* only int16/uint16 handled */

  /* Escape hatch: HPSV_DISABLE_FAST_READ=1 forces the nc_get_var fallback (for
   * A/B validation or if a future file layout ever misbehaves in production). */
  if (getenv("HPSV_DISABLE_FAST_READ")) return 1;

  /* Silence HDF5's automatic error stack printing; we handle failures. */
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);

  int rc = 1; /* default: fall back */
  hid_t file = -1, dset = -1, space = -1, dcpl = -1, dtype = -1;
  uint8_t **raw = NULL;
  hsize_t *rawsize = NULL;
  unsigned *fmask = NULL;
  haddr_t *rawaddr = NULL;
  int fd = -1;
  size_t nchunks = 0;               /* set once known; keeps cleanup safe */
  size_t chy = 0, chx = 0, nchx = 0, nchy = 0, chunk_bytes = 0;
  int16_t fillval = 0;

  file = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
  if (file < 0) goto done;

  dset = H5Dopen2(file, varname, H5P_DEFAULT);
  if (dset < 0) goto done;

  /* Rank 2, dims == {ny, nx}. */
  space = H5Dget_space(dset);
  if (space < 0 || H5Sget_simple_extent_ndims(space) != 2) goto done;
  hsize_t dims[2];
  if (H5Sget_simple_extent_dims(space, dims, NULL) < 0) goto done;
  if (dims[0] != ny || dims[1] != nx) goto done;

  /* Little-endian, 2-byte integer element. */
  dtype = H5Dget_type(dset);
  if (dtype < 0 || H5Tget_size(dtype) != elem_size ||
      H5Tget_order(dtype) != H5T_ORDER_LE)
    goto done;

  /* Chunked layout, filters == shuffle (index 0) then deflate (index 1). */
  dcpl = H5Dget_create_plist(dset);
  if (dcpl < 0 || H5Pget_layout(dcpl) != H5D_CHUNKED) goto done;
  hsize_t cdims[2];
  if (H5Pget_chunk(dcpl, 2, cdims) < 0) goto done;
  chy = (size_t)cdims[0];
  chx = (size_t)cdims[1];
  if (chy == 0 || chx == 0) goto done;

  if (H5Pget_nfilters(dcpl) != 2) goto done;
  for (unsigned fi = 0; fi < 2; fi++) {
    unsigned flags = 0, cd[8];
    size_t cd_n = 8;
    H5Z_filter_t fid =
        H5Pget_filter2(dcpl, fi, &flags, &cd_n, cd, 0, NULL, NULL);
    if (fi == 0 && fid != H5Z_FILTER_SHUFFLE) goto done;
    if (fi == 1 && fid != H5Z_FILTER_DEFLATE) goto done;
  }

  /* Fill value for any unallocated chunks (all-fill regions). */
  if (H5Pget_fill_value(dcpl, dtype, &fillval) < 0) fillval = 0;

  nchx = (nx + chx - 1) / chx;
  nchy = (ny + chy - 1) / chy;
  nchunks = nchx * nchy;
  chunk_bytes = chy * chx * elem_size;

  raw = (uint8_t **)calloc(nchunks, sizeof(uint8_t *));
  rawsize = (hsize_t *)calloc(nchunks, sizeof(hsize_t));
  fmask = (unsigned *)calloc(nchunks, sizeof(unsigned));
  rawaddr = (haddr_t *)calloc(nchunks, sizeof(haddr_t));
  if (!raw || !rawsize || !fmask || !rawaddr) goto done;

  /* --- Serial phase: locate every chunk, then pull its raw bytes. HDF5 is
   * single-locked, so neither half can be parallelized, and this phase — not the
   * inflate below — dominates the load of a full-disk 0.5 km band.
   *
   * The index half is the expensive one, and it scales quadratically: on C02
   * (G19 full disk) measured 2304 chunks -> 0.042 s but 9216 chunks -> 0.676 s,
   * i.e. 4x the chunks for 16x the time, because H5Dget_chunk_info_by_coord
   * re-walks the dataset's chunk index on every single call. The fetch half via
   * H5Dread_chunk stays linear (~20 us/chunk) and is not a problem.
   *
   * So: get the whole index in one pass with H5Dchunk_iter (HDF5 >= 1.14), then
   * fetch. Older HDF5 (Rocky 8 ships 1.10.x) keeps the per-chunk lookup, which
   * is slow but correct. Both fill rawsize[]/fmask[]; rawsize[k] > 0 marks an
   * allocated chunk, 0 means an all-fill region to be filled in below. --- */
  double t_index = 0.0, t_fetch = 0.0;
  size_t n_alloc = 0;
  bool read_ok = true;
  double t_serial0 = omp_get_wtime();

#if H5_VERSION_GE(1, 14, 0)
  {
    ChunkIndex idx = {rawsize, fmask, rawaddr, nchx, nchy, chy, chx, true};
    double t0i = omp_get_wtime();
    if (H5Dchunk_iter(dset, H5P_DEFAULT, chunk_index_cb, &idx) < 0 || !idx.ok)
      read_ok = false;
    t_index = omp_get_wtime() - t0i;
  }
#else
  for (size_t cy = 0; cy < nchy && read_ok; cy++) {
    for (size_t cx = 0; cx < nchx; cx++) {
      size_t k = cy * nchx + cx;
      hsize_t offset[2] = {(hsize_t)(cy * chy), (hsize_t)(cx * chx)};
      haddr_t addr = HADDR_UNDEF;
      hsize_t csize = 0;
      unsigned mask = 0;
      double t0i = omp_get_wtime();
      herr_t info_err = H5Dget_chunk_info_by_coord(dset, offset, &mask, &addr, &csize);
      t_index += omp_get_wtime() - t0i;
      if (info_err < 0) { read_ok = false; break; }
      if (addr == HADDR_UNDEF || csize == 0) continue; /* unallocated -> fill */
      rawsize[k] = csize;
      fmask[k] = mask;
    }
  }
#endif
  if (!read_ok) goto done;

  /* --- Fetch: leer los bytes crudos de cada chunk. ---
   *
   * H5Dread_chunk obliga a ir en serie (HDF5 tiene un lock global), pero el
   * índice ya nos dio el offset de cada chunk DENTRO DEL ARCHIVO, así que se
   * pueden leer con pread() en paralelo y saltarse HDF5 por completo. pread es
   * seguro entre hilos: no comparte el offset del descriptor.
   *
   * addr es relativo a la dirección base del archivo, que NO es 0 si el archivo
   * tiene user block. Se consulta y se suma; con user block 0 (el caso de
   * netCDF-4) la suma es inocua.
   *
   * Si algo impide el camino directo (no se pudo abrir, no hay addr, HDF5 < 1.14
   * que no llena rawaddr) se cae a H5Dread_chunk, que sigue siendo correcto. */
  bool use_pread = false;
  hsize_t base_addr = 0;
#if H5_VERSION_GE(1, 14, 0)
  {
    hid_t fcpl = H5Fget_create_plist(file);
    if (fcpl >= 0) {
      hsize_t ub = 0;
      if (H5Pget_userblock(fcpl, &ub) >= 0) base_addr = ub;
      H5Pclose(fcpl);
      /* HPSV_NO_PREAD=1 fuerza H5Dread_chunk, para A/B de rendimiento. */
      if (!getenv("HPSV_NO_PREAD")) {
        fd = open(filename, O_RDONLY);
        use_pread = (fd >= 0);
      }
    }
  }
#endif

  double t0f = omp_get_wtime();
  if (use_pread) {
    int failed_read = 0;
#pragma omp parallel for schedule(static) reduction(+ : n_alloc)
    for (size_t k = 0; k < nchunks; k++) {
      if (rawsize[k] == 0 || failed_read) continue; /* all-fill region */
      if (rawaddr[k] == HADDR_UNDEF) {
#pragma omp atomic write
        failed_read = 1;
        continue;
      }
      uint8_t *buf = (uint8_t *)malloc(rawsize[k]);
      if (!buf) {
#pragma omp atomic write
        failed_read = 1;
        continue;
      }
      /* pread puede devolver menos de lo pedido; hay que insistir. */
      size_t got = 0;
      bool bad = false;
      while (got < rawsize[k]) {
        ssize_t n = pread(fd, buf + got, rawsize[k] - got,
                          (off_t)(rawaddr[k] + base_addr + got));
        if (n <= 0) { bad = true; break; }
        got += (size_t)n;
      }
      if (bad) {
        free(buf);
#pragma omp atomic write
        failed_read = 1;
        continue;
      }
      raw[k] = buf;
      n_alloc++;
    }
    if (failed_read) {
      /* Deshacer lo leído y reintentar por el camino de HDF5. */
      for (size_t k = 0; k < nchunks; k++) { free(raw[k]); raw[k] = NULL; }
      n_alloc = 0;
      use_pread = false;
      LOG_WARN("Lectura directa de chunks falló; se usa H5Dread_chunk.");
    }
  }
  if (!use_pread) {
    for (size_t k = 0; k < nchunks; k++) {
      if (rawsize[k] == 0) { raw[k] = NULL; continue; } /* all-fill region */
      hsize_t offset[2] = {(hsize_t)((k / nchx) * chy), (hsize_t)((k % nchx) * chx)};
      raw[k] = (uint8_t *)malloc(rawsize[k]);
      if (!raw[k]) { read_ok = false; break; }
      unsigned mask = fmask[k];
      herr_t read_err = H5Dread_chunk(dset, H5P_DEFAULT, offset, &mask, raw[k]);
      if (read_err < 0) { read_ok = false; break; }
      n_alloc++;
    }
  }
  t_fetch = omp_get_wtime() - t0f;
  if (!read_ok) goto done;
  LOG_TIMING(omp_get_wtime() - t_serial0, "NetCDF chunk index+fetch");
  LOG_DEBUG("  %zu chunks (%zu allocated): index %.3f s, fetch %.3f s (%s)",
            nchunks, n_alloc, t_index, t_fetch,
            use_pread ? "pread paralelo" : "H5Dread_chunk serial");

  /* --- Parallel phase: inflate + unshuffle + scatter. --- */
  const unsigned SHUF_BIT = 0x1u; /* pipeline index 0 skipped */
  const unsigned DEFL_BIT = 0x2u; /* pipeline index 1 skipped */
  int failed = 0;
  double t0 = omp_get_wtime();

#pragma omp parallel
  {
    struct libdeflate_decompressor *dec = libdeflate_alloc_decompressor();
    uint8_t *shuf = (uint8_t *)malloc(chunk_bytes);
    uint8_t *elems = (uint8_t *)malloc(chunk_bytes);
    if (!dec || !shuf || !elems) {
#pragma omp atomic write
      failed = 1;
    }

#pragma omp for schedule(static)
    for (size_t k = 0; k < nchunks; k++) {
      if (failed) continue;
      size_t cy = k / nchx, cx = k % nchx;
      size_t r0 = cy * chy, c0 = cx * chx;

      const uint8_t *elem_bytes;
      if (raw[k] == NULL) {
        int16_t *e16 = (int16_t *)elems; /* unallocated chunk -> fill */
        for (size_t i = 0; i < chy * chx; i++) e16[i] = fillval;
        elem_bytes = elems;
      } else {
        const uint8_t *inflated;
        if (fmask[k] & DEFL_BIT) {
          inflated = raw[k]; /* deflate skipped for this chunk */
        } else {
          size_t got = 0;
          if (libdeflate_zlib_decompress(dec, raw[k], rawsize[k], shuf,
                                         chunk_bytes,
                                         &got) != LIBDEFLATE_SUCCESS ||
              got != chunk_bytes) {
#pragma omp atomic write
            failed = 1;
            continue;
          }
          inflated = shuf;
        }
        if (fmask[k] & SHUF_BIT) {
          elem_bytes = inflated; /* shuffle skipped -> already interleaved */
        } else {
          unshuffle(inflated, elems, chy * chx, elem_size);
          elem_bytes = elems;
        }
      }

      /* Scatter the chunk tile into out, clipping partial edge chunks. */
      size_t rmax = (r0 + chy <= ny) ? chy : (ny - r0);
      size_t cmax = (c0 + chx <= nx) ? chx : (nx - c0);
      for (size_t lr = 0; lr < rmax; lr++) {
        uint8_t *dst = (uint8_t *)out + ((r0 + lr) * nx + c0) * elem_size;
        const uint8_t *src = elem_bytes + (lr * chx) * elem_size;
        memcpy(dst, src, cmax * elem_size);
      }
    }

    free(shuf);
    free(elems);
    if (dec) libdeflate_free_decompressor(dec);
  }

  if (!failed) {
    LOG_TIMING(omp_get_wtime() - t0, "NetCDF chunked decompress (libdeflate)");
    rc = 0;
  }

done:
  if (raw) {
    for (size_t k = 0; k < nchunks; k++) free(raw[k]);
    free(raw);
  }
  free(rawsize);
  free(fmask);
  if (dtype >= 0) H5Tclose(dtype);
  if (dcpl >= 0) H5Pclose(dcpl);
  if (space >= 0) H5Sclose(space);
  free(rawaddr);
  if (fd >= 0) close(fd);
  if (dset >= 0) H5Dclose(dset);
  if (file >= 0) H5Fclose(file);
  return rc;
}
