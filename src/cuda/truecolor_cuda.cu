/* Device-resident true-color kernels: synthetic green + RGB compose.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Ambos kernels operan sobre buffers ya residentes en device (DataFDev). La
 * subida H2D de C01/C02/C03 la hace dataf_dev_upload() una sola vez; aquí solo
 * se computa y, en el compose, se baja la imagen uint8 de salida.
 */

#include "cuda_common.cuh"
#include <stdint.h>

extern "C" {
#include "cuda_truecolor.h"
#include "logger.h"
#include "timing.h"
}

/* NonData de host (src/datanc.c) = 1.0e+32; satisface is_nondata_dev (>=1e30). */
#define HPSV_NONDATA_DEV 1.0e+32f

#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                             \
      LOG_ERROR("CUDA error en %s:%d: %s", __FILE__, __LINE__,               \
                cudaGetErrorString(err__));                                  \
      goto cleanup;                                                          \
    }                                                                        \
  } while (0)

/* ---- verde sintético -------------------------------------------------------
 * Un hilo por píxel. Idéntico al cuerpo de create_truecolor_synthetic_green(),
 * salvo que no acumula min/max (no se usan en truecolor). */
__global__ void green_kernel(const float *b, const float *r, const float *nir,
                             float *g, size_t n) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  float B = b[i], R = r[i], N = nir[i];
  if (is_nondata_dev(B) || is_nondata_dev(R) || is_nondata_dev(N)) {
    g[i] = HPSV_NONDATA_DEV;
  } else {
    g[i] = (0.465f * B) + (0.465f * R) + (0.07f * N);
  }
}

extern "C" DataFDev create_truecolor_green_from_dev(const DataFDev *blue,
                                                    const DataFDev *red,
                                                    const DataFDev *nir) {
  DataFDev green = {0, 0, 0, NULL, 0.0f, 0.0f};
  if (!blue || !red || !nir || !blue->d_data || !red->d_data || !nir->d_data) {
    LOG_ERROR("create_truecolor_green_from_dev: input DataFDev is NULL.");
    return green;
  }
  if (blue->width != red->width || blue->height != red->height ||
      blue->width != nir->width || blue->height != nir->height) {
    LOG_ERROR("Dimension mismatch in TrueColor green (CUDA).");
    return green;
  }

  size_t n = blue->size;
  bool ok = false;
  float *d_g = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((n + block - 1) / block);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaMalloc((void **)&d_g, n * sizeof(float)));
  CUDA_CHECK(cudaEventRecord(t0));

  green_kernel<<<grid, block>>>(blue->d_data, red->d_data, nir->d_data, d_g, n);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_COMPOSE, ms / 1000.0, "Synthetic green (CUDA, device-resident)");
  ok = true;

cleanup:
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    if (d_g) cudaFree(d_g);
    return green;
  }
  green.width = blue->width;
  green.height = blue->height;
  green.size = n;
  green.d_data = d_g;
  return green;
}

/* ---- compose RGB -----------------------------------------------------------
 * Un hilo por píxel de salida; escribe 3 bytes. Idéntico a create_multiband_rgb(). */
__device__ __forceinline__ unsigned char norm_byte(float v, float mn,
                                                    float range) {
  if (is_nondata_dev(v)) return 0;
  float norm = (v - mn) / range;
  norm = fminf(fmaxf(norm, 0.0f), 1.0f);
  return (unsigned char)(norm * 255.0f);
}

__global__ void multiband_kernel(const float *r, const float *g, const float *b,
                                 unsigned char *out, size_t n, float r_min,
                                 float r_range, float g_min, float g_range,
                                 float b_min, float b_range) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  size_t idx = i * 3;
  out[idx] = norm_byte(r[i], r_min, r_range);
  out[idx + 1] = norm_byte(g[i], g_min, g_range);
  out[idx + 2] = norm_byte(b[i], b_min, b_range);
}

extern "C" ImageData create_multiband_rgb_from_dev(const DataFDev *r,
                                                   const DataFDev *g,
                                                   const DataFDev *b,
                                                   float r_min, float r_max,
                                                   float g_min, float g_max,
                                                   float b_min, float b_max,
                                                   unsigned char **d_retain) {
  if (d_retain) *d_retain = NULL;
  if (!r || !g || !b || !r->d_data || !g->d_data || !b->d_data) {
    LOG_ERROR("create_multiband_rgb_from_dev: input DataFDev is NULL.");
    return image_create(0, 0, 0);
  }
  if (r->width != g->width || r->height != g->height || r->width != b->width ||
      r->height != b->height) {
    LOG_ERROR("Channel dimensions mismatch in create_multiband_rgb_from_dev.");
    return image_create(0, 0, 0);
  }

  ImageData imout = image_create(r->width, r->height, 3);
  if (imout.data == NULL) {
    LOG_ERROR("Memory allocation failed for RGB output image (CUDA path).");
    return imout;
  }

  float r_range = r_max - r_min;
  float g_range = g_max - g_min;
  float b_range = b_max - b_min;
  if (fabsf(r_range) < 1e-6f) r_range = 1.0f;
  if (fabsf(g_range) < 1e-6f) g_range = 1.0f;
  if (fabsf(b_range) < 1e-6f) b_range = 1.0f;

  size_t n = r->size;
  size_t out_bytes = n * 3;
  bool ok = false;
  unsigned char *d_out = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((n + block - 1) / block);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_out, out_bytes));
  multiband_kernel<<<grid, block>>>(r->d_data, g->d_data, b->d_data, d_out, n,
                                    r_min, r_range, g_min, g_range, b_min,
                                    b_range);
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaMemcpy(imout.data, d_out, out_bytes, cudaMemcpyDeviceToHost));

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_COMPOSE, ms / 1000.0, "Multiband RGB (CUDA, device-resident: kernel + D2H)");
  ok = true;

cleanup:
  /* Con d_retain el buffer sobrevive a esta función: lo hereda el llamador para
   * dárselo a la reproyección. Solo se entrega si todo salió bien; ante fallo se
   * libera aquí para no dejar un puntero colgante en manos del llamador. */
  if (ok && d_retain) {
    *d_retain = d_out;
  } else if (d_out) {
    cudaFree(d_out);
  }
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    free(imout.data);
    return image_create(0, 0, 0);
  }
  return imout;
}

extern "C" bool cuda_download_device_image(const unsigned char *d_image,
                                           unsigned char *host, size_t bytes) {
  if (!d_image || !host) return false;
  cudaError_t e = cudaMemcpy(host, d_image, bytes, cudaMemcpyDeviceToHost);
  if (e != cudaSuccess) {
    LOG_ERROR("cuda_download_device_image: %s", cudaGetErrorString(e));
    return false;
  }
  return true;
}

extern "C" void cuda_free_device_image(unsigned char *d_image) {
  if (d_image) cudaFree(d_image);
}

/* ---- piecewise stretch ----------------------------------------------------
 * Port de apply_piecewise_stretch() (src/truecolor.c). La curva es fija (5
 * puntos) y se toma de GEO2GRID_STRETCH_X/Y, la misma tabla que usa la CPU: se
 * sube a device en cada llamada porque son 40 bytes.
 *
 * No se reduce min/max como sí hace la versión CPU: con stretch activo el rango
 * de render queda fijado a [0,1] por compose_truecolor(), así que band->fmin y
 * fmax no se usan. */
__device__ __forceinline__ float interp_linear_dev(float val, const float *x,
                                                   const float *y, int n) {
  if (val <= x[0]) return y[0];
  if (val >= x[n - 1]) return y[n - 1];
  for (int i = 0; i < n - 1; i++) {
    if (val >= x[i] && val < x[i + 1]) {
      float slope = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
      return y[i] + (val - x[i]) * slope;
    }
  }
  return val;
}

__global__ void piecewise_stretch_kernel(float *data, size_t n, const float *x,
                                         const float *y, int count) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  float val = data[i];
  if (is_nondata_dev(val)) return; // NonData intacto, igual que en CPU
  data[i] = interp_linear_dev(val, x, y, count);
}

extern "C" bool apply_piecewise_stretch_dev(DataFDev *band) {
  if (!band || !band->d_data) return false;

  bool ok = false;
  float *d_x = NULL, *d_y = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((band->size + block - 1) / block);
  size_t tbytes = HPSV_STRETCH_COUNT * sizeof(float);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_x, tbytes));
  CUDA_CHECK(cudaMalloc((void **)&d_y, tbytes));
  CUDA_CHECK(cudaMemcpy(d_x, GEO2GRID_STRETCH_X, tbytes, cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_y, GEO2GRID_STRETCH_Y, tbytes, cudaMemcpyHostToDevice));

  piecewise_stretch_kernel<<<grid, block>>>(band->d_data, band->size, d_x, d_y,
                                            HPSV_STRETCH_COUNT);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_ENHANCE, ms / 1000.0, "Piecewise stretch (CUDA, device-resident)");
  band->fmin = 0.0f;
  band->fmax = 1.0f;
  ok = true;

cleanup:
  if (d_x) cudaFree(d_x);
  if (d_y) cudaFree(d_y);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  return ok;
}

/* ---- ratio sharpening -----------------------------------------------------
 * Port de dataf_ratio_sharpen_map() + los dos OP_MUL de compose_truecolor()
 * (src/truecolor.c, src/rgb.c). La CPU materializa dos arreglos intermedios —el
 * promedio 2x2 y el mapa de razones— y recorre la imagen tres veces; aquí cada
 * hilo recalcula el promedio de su propio bloque 2x2 y multiplica verde y azul
 * en sitio, en una sola pasada y sin reservar nada.
 *
 * El promedio se acumula en el mismo orden que la CPU (k = 0..3 sobre
 * {(x0,y0),(x1,y0),(x0,y1),(x1,y1)}) porque la suma en float no es asociativa:
 * cambiar el orden daría un último bit distinto y rompería la comparación
 * byte a byte de tests/test_cuda.sh. */
__global__ void ratio_sharpen_kernel(const float *red, float *g, float *b,
                                     unsigned int w, unsigned int h) {
  unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= w || y >= h) return;

  /* Origen del bloque 2x2 al que pertenece este pixel; el borde impar se
   * duplica sobre si mismo, igual que dataf_mean_2x2(). */
  unsigned int x0 = (x & ~1u), y0 = (y & ~1u);
  unsigned int x1 = (x0 + 1 < w) ? x0 + 1 : x0;
  unsigned int y1 = (y0 + 1 < h) ? y0 + 1 : y0;

  const float vals[4] = {red[(size_t)y0 * w + x0], red[(size_t)y0 * w + x1],
                         red[(size_t)y1 * w + x0], red[(size_t)y1 * w + x1]};
  float sum = 0.0f;
  int count = 0;
  for (int k = 0; k < 4; k++) {
    if (!is_nondata_dev(vals[k])) {
      sum += vals[k];
      count++;
    }
  }
  float mean = (count > 0) ? sum / count : HPSV_NONDATA_DEV;

  size_t i = (size_t)y * w + x;
  float ch = red[i];
  float ratio;
  if (is_nondata_dev(ch) || is_nondata_dev(mean) || mean == 0.0f) {
    ratio = 1.0f;
  } else {
    ratio = ch / mean;
    if (!isfinite(ratio) || ratio < 0.0f) ratio = 1.0f;
    if (ratio < 0.5f) ratio = 0.5f;
    if (ratio > 1.5f) ratio = 1.5f;
  }

  /* dataf_op_dataf(OP_MUL) propaga NonData desde cualquiera de los dos
   * operandos; el mapa de razones nunca lo es, asi que basta con mirar el
   * canal. */
  float gv = g[i];
  if (!is_nondata_dev(gv)) g[i] = gv * ratio;
  float bv = b[i];
  if (!is_nondata_dev(bv)) b[i] = bv * ratio;
}

extern "C" bool apply_ratio_sharpen_dev(const DataFDev *red, DataFDev *green,
                                        DataFDev *blue) {
  if (!red || !red->d_data || !green || !green->d_data || !blue ||
      !blue->d_data)
    return false;
  if (green->width != red->width || green->height != red->height ||
      blue->width != red->width || blue->height != red->height) {
    LOG_ERROR("apply_ratio_sharpen_dev: dimensiones distintas entre canales.");
    return false;
  }

  bool ok = false;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  dim3 block(16, 16);
  dim3 grid((red->width + block.x - 1) / block.x,
            (red->height + block.y - 1) / block.y);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  ratio_sharpen_kernel<<<grid, block>>>(red->d_data, green->d_data,
                                        blue->d_data, red->width, red->height);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_ENHANCE, ms / 1000.0, "Ratio sharpening (CUDA, device-resident)");
  ok = true;

cleanup:
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  return ok;
}
