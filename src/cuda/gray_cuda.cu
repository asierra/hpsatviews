/* Gray view generation - CUDA port of create_single_gray() in gray.c
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Diseño:
 * - Un hilo CUDA procesa exactamente un píxel (igual que una iteración
 *   del `#pragma omp parallel for` original).
 * - Los arreglos de entrada/salida son planos (row-major), igual que en
 *   la versión CPU: no se necesita ningún cambio de layout.
 * - La entrada float ya reside en device (DataFDev): este archivo solo
 *   reserva y baja la imagen uint8 de salida. La subida H2D del float la hace
 *   dataf_dev_upload() una sola vez, amortizada sobre la cadena de kernels.
 */

#include "cuda_common.cuh"
#include <stdint.h>

extern "C" {
#include "cuda_dataf.h"
#include "logger.h"
}

/* ---- Macro de chequeo de errores CUDA ----
 * Envuelve cada llamada a la API de CUDA. Si algo falla salta a cleanup para
 * liberar todos los recursos ya reservados (buffer de device de salida,
 * eventos y la imagen de host). */
#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                             \
      LOG_ERROR("CUDA error en %s:%d: %s", __FILE__, __LINE__,               \
                cudaGetErrorString(err__));                                  \
      goto cleanup;                                                          \
    }                                                                        \
  } while (0)

/* Un hilo por píxel. Lógica idéntica al cuerpo del for original. */
__global__ void gray_kernel(const float *data_in, unsigned char *out,
                            unsigned int width, unsigned int height,
                            unsigned int bpp, float min_val, float max_val,
                            float range, bool invert_value, bool use_alpha,
                            uint8_t last_color, bool has_nan_color) {
  unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) return;

  unsigned int i = y * width + x;
  unsigned int po = i * bpp;
  uint8_t r = 0, a = 0;

  float val = data_in[i];
  if (!is_nondata_dev(val)) {
    if (val < min_val) val = min_val;
    if (val > max_val) val = max_val;

    float normalized_val = invert_value ? (max_val - val) / range
                                        : (val - min_val) / range;

    r = (uint8_t)(last_color * normalized_val);
    a = 255;
  } else {
    if (use_alpha) {
      r = 0;
      a = 0;
    } else if (has_nan_color) {
      r = last_color;
      a = 255;
    } else {
      r = 0;
      a = 0;
    }
  }

  out[po] = r;
  if (bpp == 2) {
    out[po + 1] = a;
  }
}

extern "C" ImageData create_single_gray_from_dev(const DataFDev *dev,
                                                 bool invert_value,
                                                 bool use_alpha, float min_val,
                                                 float max_val,
                                                 const CPTData *cpt) {
  unsigned int bpp = use_alpha ? 2 : 1;
  ImageData imout = image_create(dev->width, dev->height, bpp);
  if (imout.data == NULL) {
    LOG_ERROR("Failed to allocate memory for gray image (CUDA path).");
    return imout;
  }

  uint8_t last_color =
      (cpt && cpt->has_nan_color) ? (uint8_t)(cpt->num_colors - 1) : 255;
  bool has_nan_color = cpt && cpt->has_nan_color;

  float range = max_val - min_val;
  if (range == 0.0f) range = 1.0f;

  size_t n_pixels = dev->size;
  size_t out_bytes = n_pixels * bpp * sizeof(unsigned char);

  /* Todo lo que cleanup libera se declara antes del primer CUDA_CHECK. */
  bool ok = false;
  unsigned char *d_out = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  dim3 block(16, 16);
  dim3 grid((dev->width + block.x - 1) / block.x,
            (dev->height + block.y - 1) / block.y);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_out, out_bytes));

  gray_kernel<<<grid, block>>>(dev->d_data, d_out, dev->width, dev->height, bpp,
                               min_val, max_val, range, invert_value, use_alpha,
                               last_color, has_nan_color);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaMemcpy(imout.data, d_out, out_bytes, cudaMemcpyDeviceToHost));

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING(ms / 1000.0, "Single Gray (CUDA, device-resident: kernel + D2H)");

  ok = true;

cleanup:
  if (d_out) cudaFree(d_out);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);

  if (!ok) {
    free(imout.data);
    return image_create(0, 0, 0); /* ImageData con data == NULL = fallo */
  }
  return imout;
}
