/* Fast chunked NetCDF-4/HDF5 variable read: bypasses HDF5's serial filter
 * pipeline by reading raw chunks and decompressing them in parallel with
 * libdeflate (the GOES ABI files use shuffle + deflate).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_READER_NC_CHUNK_H_
#define HPSATVIEWS_READER_NC_CHUNK_H_

#include <stddef.h>

/**
 * Reads variable `varname` from NetCDF-4 file `filename` into `out`, filling it
 * with the raw (unpacked-but-not-scaled) element values exactly as nc_get_var
 * would — but decompressing the HDF5 chunks in parallel with libdeflate instead
 * of HDF5's single-threaded filter pipeline.
 *
 * Only handles the common GOES layout: a 2-D chunked dataset filtered with
 * shuffle + deflate (gzip), little-endian, element size == elem_size. Anything
 * else (other filters, byte order, rank) returns non-zero so the caller can
 * fall back to nc_get_var().
 *
 * @param out       Destination buffer of nx*ny elements of elem_size bytes (row-major).
 * @param nx, ny    Grid width/height (x = fastest-varying dimension).
 * @param elem_size Bytes per element (2 for int16/uint16).
 * @return 0 on success; non-zero if unsupported (caller must fall back).
 */
int read_var_chunked_deflate(const char *filename, const char *varname,
                             void *out, size_t nx, size_t ny, size_t elem_size);

#endif /* HPSATVIEWS_READER_NC_CHUNK_H_ */
