/* Device-resident DataF: lifecycle (upload/destroy) + element-wise kernels
 * that operate in place on the GPU buffer (gamma, for now).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Diseño: mantener el buffer float en device entre operaciones para amortizar
 * la transferencia H2D sobre una cadena de kernels (gamma -> gray -> ...). Cada
 * operación loguea su propio [PERF] para exponer el desglose transferencia vs
 * cómputo (ver docs/cuda-support/CUDA_PLAN.md).
 */

#include "cuda_common.cuh"
#include <stdint.h>

extern "C" {
#include "cuda_dataf.h"
#include "logger.h"
}

/* ---- upload ---------------------------------------------------------------
 * Reserva un buffer de device y copia el grid float desde host. El tiempo
 * medido incluye cudaMalloc + H2D (el costo real de "poner los datos en la
 * GPU"), consistente con cómo se midió el kernel gray original. */
extern "C" DataFDev dataf_dev_upload(const DataF *host) {
  DataFDev dev = {0, 0, 0, NULL, 0.0f, 0.0f};
  if (!host || !host->data_in) {
    LOG_ERROR("dataf_dev_upload: host data is NULL.");
    return dev;
  }

  /* La primera llamada a la API de CUDA del proceso crea el contexto (cientos
   * de ms). Se fuerza una sola vez fuera de toda medición. */
  static bool warmed = false;
  if (!warmed) {
    cudaFree(0);
    warmed = true;
  }

  size_t n = (size_t)host->width * host->height;
  size_t bytes = n * sizeof(float);

  float *d = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  cudaEventCreate(&t0);
  cudaEventCreate(&t1);
  cudaEventRecord(t0);

  cudaError_t e = cudaMalloc((void **)&d, bytes);
  if (e == cudaSuccess) {
    e = cudaMemcpy(d, host->data_in, bytes, cudaMemcpyHostToDevice);
  }
  cudaEventRecord(t1);
  cudaEventSynchronize(t1);

  if (e != cudaSuccess) {
    LOG_ERROR("dataf_dev_upload: %s", cudaGetErrorString(e));
    if (d) cudaFree(d);
    cudaEventDestroy(t0);
    cudaEventDestroy(t1);
    return dev;
  }

  float ms = 0.0f;
  cudaEventElapsedTime(&ms, t0, t1);
  cudaEventDestroy(t0);
  cudaEventDestroy(t1);
  LOG_TIMING(ms / 1000.0, "DataF upload (cudaMalloc + H2D)");

  dev.width = host->width;
  dev.height = host->height;
  dev.size = n;
  dev.d_data = d;
  dev.fmin = host->fmin;
  dev.fmax = host->fmax;
  return dev;
}

/* ---- alloc (sin upload) --------------------------------------------------- */
extern "C" DataFDev dataf_dev_alloc(unsigned int width, unsigned int height) {
  DataFDev dev = {0, 0, 0, NULL, 0.0f, 0.0f};
  size_t n = (size_t)width * height;
  float *d = NULL;
  cudaError_t e = cudaMalloc((void **)&d, n * sizeof(float));
  if (e != cudaSuccess) {
    LOG_ERROR("dataf_dev_alloc: %s", cudaGetErrorString(e));
    return dev;
  }
  dev.width = width;
  dev.height = height;
  dev.size = n;
  dev.d_data = d;
  return dev;
}

/* ---- destroy -------------------------------------------------------------- */
extern "C" void dataf_dev_destroy(DataFDev *dev) {
  if (!dev) return;
  if (dev->d_data) cudaFree(dev->d_data);
  dev->d_data = NULL;
  dev->width = dev->height = 0;
  dev->size = 0;
}

/* ---- gamma ----------------------------------------------------------------
 * Un hilo por píxel (grid 1D). Réplica exacta del cuerpo de
 * dataf_apply_gamma() (src/datanc.c): normaliza, clampa a [0,1] antes de powf
 * (evita NaN por dominio negativo) y preserva NonData. */
__global__ void gamma_kernel(float *data, size_t n, float min_val, float range,
                             float inv_gamma) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  float val = data[i];
  if (is_nondata_dev(val)) return;

  float norm = (val - min_val) / range;
  norm = fminf(fmaxf(norm, 0.0f), 1.0f);
  data[i] = powf(norm, inv_gamma);
}

extern "C" void dataf_dev_apply_gamma(DataFDev *dev, float gamma, float min_val,
                                      float max_val) {
  if (!dev || !dev->d_data) return;

  /* Mismos cortocircuitos que la versión CPU: gamma==1 y rango degenerado. */
  if (fabsf(gamma - 1.0f) < 1e-6f) return;
  float range = max_val - min_val;
  if (range <= 0.0f || IS_NONDATA(min_val)) return;

  float inv_gamma = 1.0f / gamma;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((dev->size + block - 1) / block);

  cudaEvent_t t0 = NULL, t1 = NULL;
  cudaEventCreate(&t0);
  cudaEventCreate(&t1);
  cudaEventRecord(t0);

  gamma_kernel<<<grid, block>>>(dev->d_data, dev->size, min_val, range,
                                inv_gamma);

  cudaEventRecord(t1);
  cudaEventSynchronize(t1);
  cudaError_t e = cudaGetLastError();

  float ms = 0.0f;
  cudaEventElapsedTime(&ms, t0, t1);
  cudaEventDestroy(t0);
  cudaEventDestroy(t1);

  if (e != cudaSuccess) {
    LOG_ERROR("dataf_dev_apply_gamma: %s", cudaGetErrorString(e));
    return;
  }
  LOG_TIMING(ms / 1000.0, "Gamma (CUDA, device-resident)");

  /* Tras gamma el rango normalizado es [0,1], igual que la versión CPU. */
  dev->fmin = 0.0f;
  dev->fmax = 1.0f;
}
