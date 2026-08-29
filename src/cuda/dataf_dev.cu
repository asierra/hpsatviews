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
#include <stdlib.h>
#include <time.h>

extern "C" {
#include "cuda_dataf.h"
#include "logger.h"
#include "timing.h"
}

/* Reloj de pared: el costo de esta función es host-side (cudaHostRegister y
 * cudaMalloc no son operaciones de stream), así que medirlo con cudaEvent
 * dejaría fuera justo la parte cara. */
static double wall_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- upload ---------------------------------------------------------------
 * Reserva un buffer de device y copia el grid float desde host. El tiempo
 * medido incluye cudaHostRegister + cudaMalloc + H2D: el costo real de "poner
 * los datos en la GPU", sin partes escondidas.
 *
 * Sobre el pinning: un cudaMemcpy desde memoria paginable no llega a la
 * velocidad del bus porque el driver copia antes a un buffer pinned interno
 * (medido en tahan: 11.6 GB/s contra 23.6 GB/s pinned). Registrar el buffer de
 * host con cudaHostRegister deja que el DMA lea directo de él.
 *
 * Se registra en vez de reservar con cudaHostAlloc a propósito: fijar páginas en
 * la reserva cuesta 0.141 s por 470 MB, más que los 0.043 s de la copia paginable
 * entera, y no se amortiza porque cada DataF se sube una sola vez. Registrar el
 * buffer que ya existe cuesta 0.010 s en tahan → 0.043 s se vuelven 0.031 s.
 *
 * PERO el costo de registrar depende mucho de la máquina: los mismos 470 MB
 * cuestan 0.048 s en una RTX 5060 Ti de escritorio, donde pinnear PIERDE. Por eso
 * es desactivable con HPSV_NO_PINNED_UPLOAD=1, y el log reporta el desglose para
 * que se pueda decidir con datos en cada host. */
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

  /* Por debajo de este tamaño el registro no se paga: son dos llamadas más a la
   * API para mover unos pocos MB. Un full-disk son cientos de MB. */
  const size_t kPinnedMinBytes = (size_t)16 << 20;

  float *d = NULL;
  double t_start = wall_seconds();
  double t_register = 0.0;

  bool pinned = false;
  if (bytes >= kPinnedMinBytes && !getenv("HPSV_NO_PINNED_UPLOAD")) {
    double t0r = wall_seconds();
    /* El const del parámetro es del struct, no del contenido: registrar no
     * modifica los datos, solo fija las páginas. */
    if (cudaHostRegister((void *)host->data_in, bytes, cudaHostRegisterDefault) ==
        cudaSuccess) {
      pinned = true;
    } else {
      /* No es fatal (p.ej. el rango ya estaba registrado, o el sistema no lo
       * permite): se sigue con la copia paginable. Hay que consumir el error
       * para no contaminar el siguiente cudaGetLastError() del pipeline. */
      cudaGetLastError();
    }
    t_register = wall_seconds() - t0r;
  }

  cudaError_t e = cudaMalloc((void **)&d, bytes);
  if (e == cudaSuccess) {
    e = cudaMemcpy(d, host->data_in, bytes, cudaMemcpyHostToDevice);
  }

  if (pinned) cudaHostUnregister((void *)host->data_in);
  double elapsed = wall_seconds() - t_start;

  if (e != cudaSuccess) {
    LOG_ERROR("dataf_dev_upload: %s", cudaGetErrorString(e));
    if (d) cudaFree(d);
    return dev;
  }

  LOG_TIMING_STAGE(TM_XFER, elapsed, "DataF upload (cudaMalloc + H2D%s)",
             pinned ? ", pinned" : "");
  /* Ancho de banda efectivo y costo del registro: es lo que permite decidir por
   * host si conviene pinnear (comparar contra una corrida con
   * HPSV_NO_PINNED_UPLOAD=1). */
  if (pinned) {
    LOG_DEBUG("  %.0f MB a %.1f GB/s efectivos (registro %.3f s)",
              bytes / (1024.0 * 1024.0), (bytes / 1e9) / elapsed, t_register);
  }

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

/* ---- download ------------------------------------------------------------- */
extern "C" bool dataf_dev_download(const DataFDev *dev, DataF *host_out) {
  if (!host_out) return false;
  memset(host_out, 0, sizeof(*host_out));
  if (!dev || !dev->d_data) return false;

  DataF host = dataf_create(dev->width, dev->height);
  if (!host.data_in) {
    LOG_ERROR("dataf_dev_download: host allocation failed.");
    return false;
  }
  cudaError_t e = cudaMemcpy(host.data_in, dev->d_data, dev->size * sizeof(float),
                             cudaMemcpyDeviceToHost);
  if (e != cudaSuccess) {
    LOG_ERROR("dataf_dev_download: %s", cudaGetErrorString(e));
    dataf_destroy(&host);
    return false;
  }
  host.fmin = dev->fmin;
  host.fmax = dev->fmax;
  *host_out = host;
  return true;
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
  LOG_TIMING_STAGE(TM_ENHANCE, ms / 1000.0, "Gamma (CUDA, device-resident)");

  /* Tras gamma el rango normalizado es [0,1], igual que la versión CPU. */
  dev->fmin = 0.0f;
  dev->fmax = 1.0f;
}
