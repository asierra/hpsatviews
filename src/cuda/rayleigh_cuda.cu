/* Device-resident Rayleigh (LUT) + solar-zenith correction.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Port directo de apply_solar_zenith_correction() (src/truecolor.c) y
 * luts_rayleigh_correction()/get_rayleigh_value() (src/rayleigh.c). La tabla LUT
 * se parsea en host (rayleigh_lut_load_from_memory, reusada) y se sube a memoria
 * global de device. Un hilo por píxel; sin estadísticas ni recálculo de
 * fmin/fmax (diagnóstico/no usado en truecolor).
 */

#include "cuda_common.cuh"
#include <stdint.h>

extern "C" {
#include "cuda_rayleigh.h"
#include "rayleigh.h"
#include "logger.h"
#include "timing.h"
}

#define HPSV_PI_F 3.14159265358979323846f
#define HPSV_DEG2RAD (HPSV_PI_F / 180.0f)

#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                             \
      LOG_ERROR("CUDA error en %s:%d: %s", __FILE__, __LINE__,               \
                cudaGetErrorString(err__));                                  \
      goto cleanup;                                                          \
    }                                                                        \
  } while (0)

/* ---- carga/subida de la LUT ---------------------------------------------- */
extern "C" RayleighLUTDev rayleigh_lut_dev_load(uint8_t channel) {
  RayleighLUTDev dev = {};
  RayleighLUT host = rayleigh_lut_load_from_memory(channel);
  if (!host.table) {
    LOG_ERROR("rayleigh_lut_dev_load: host LUT load failed (channel %d).", channel);
    return dev;
  }

  size_t count = (size_t)host.n_sz * host.n_vz * host.n_az;
  float *d_table = NULL;
  cudaError_t e = cudaMalloc((void **)&d_table, count * sizeof(float));
  if (e == cudaSuccess) {
    e = cudaMemcpy(d_table, host.table, count * sizeof(float), cudaMemcpyHostToDevice);
  }
  if (e != cudaSuccess) {
    LOG_ERROR("rayleigh_lut_dev_load: %s", cudaGetErrorString(e));
    if (d_table) cudaFree(d_table);
    rayleigh_lut_destroy(&host);
    return dev;
  }

  dev.d_table = d_table;
  dev.channel = channel;
  dev.n_sz = host.n_sz; dev.n_vz = host.n_vz; dev.n_az = host.n_az;
  dev.sz_min = host.sz_min; dev.sz_max = host.sz_max; dev.sz_step = host.sz_step;
  dev.vz_min = host.vz_min; dev.vz_max = host.vz_max; dev.vz_step = host.vz_step;
  dev.az_min = host.az_min; dev.az_max = host.az_max; dev.az_step = host.az_step;

  rayleigh_lut_destroy(&host);
  return dev;
}

extern "C" void rayleigh_lut_dev_destroy(RayleighLUTDev *lut) {
  if (!lut) return;
  if (lut->d_table) cudaFree(lut->d_table);
  lut->d_table = NULL;
  lut->n_sz = lut->n_vz = lut->n_az = 0;
}

/* ---- lookup trilineal (port de get_rayleigh_value) ----------------------- */
__device__ float get_rayleigh_value_dev(const RayleighLUTDev lut, float s,
                                         float v, float a) {
  if (s < lut.sz_min) s = lut.sz_min;
  if (s >= lut.sz_max) s = lut.sz_max;
  if (v < lut.vz_min) v = lut.vz_min;
  if (v >= lut.vz_max) v = lut.vz_max;

  // Azimuth symmetry; pyspectral convention: index with (180 - azidiff).
  a = fabsf(a);
  if (a > 180.0f) a = 360.0f - a;
  a = 180.0f - a;
  if (a > lut.az_max) a = lut.az_max;
  if (a < lut.az_min) a = lut.az_min;

  float idx_s = (s - lut.sz_min) / lut.sz_step;
  float idx_v = (v - lut.vz_min) / lut.vz_step;
  float idx_a = (a - lut.az_min) / lut.az_step;

  int s0 = (int)idx_s;
  int v0 = (int)idx_v;
  int a0 = (int)idx_a;

  int s1 = s0 + 1; if (s1 >= lut.n_sz) s1 = lut.n_sz - 1;
  int v1 = v0 + 1; if (v1 >= lut.n_vz) v1 = lut.n_vz - 1;
  int a1 = a0 + 1; if (a1 >= lut.n_az) a1 = lut.n_az - 1;

  float ds = idx_s - s0;
  float dv = idx_v - v0;
  float da = idx_a - a0;

  int stride_v = lut.n_az;
  int stride_s = lut.n_vz * lut.n_az;
  const float *t = lut.d_table;

  float c000 = t[s0 * stride_s + v0 * stride_v + a0];
  float c001 = t[s0 * stride_s + v0 * stride_v + a1];
  float c010 = t[s0 * stride_s + v1 * stride_v + a0];
  float c011 = t[s0 * stride_s + v1 * stride_v + a1];
  float c100 = t[s1 * stride_s + v0 * stride_v + a0];
  float c101 = t[s1 * stride_s + v0 * stride_v + a1];
  float c110 = t[s1 * stride_s + v1 * stride_v + a0];
  float c111 = t[s1 * stride_s + v1 * stride_v + a1];

  float c00 = c000 * (1.0f - da) + c001 * da;
  float c01 = c010 * (1.0f - da) + c011 * da;
  float c10 = c100 * (1.0f - da) + c101 * da;
  float c11 = c110 * (1.0f - da) + c111 * da;
  float c0 = c00 * (1.0f - dv) + c01 * dv;
  float c1 = c10 * (1.0f - dv) + c11 * dv;
  return c0 * (1.0f - ds) + c1 * ds;
}

/* ---- solar-zenith correction --------------------------------------------- */
__global__ void solar_zenith_kernel(float *data, const float *sza, size_t n) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  const float MAX_SZA = 85.0f;
  float refl = data[i];
  float sza_deg = sza[i];

  if (is_nondata_dev(refl) || is_nondata_dev(sza_deg) || sza_deg > MAX_SZA) {
    data[i] = 0.0f;
    return;
  }
  float cos_sza = cosf(sza_deg * HPSV_DEG2RAD);
  if (cos_sza > 0.087f) {
    data[i] = refl / cos_sza;
  } else {
    data[i] = 0.0f;
  }
}

extern "C" void apply_solar_zenith_correction_dev(DataFDev *data,
                                                  const DataFDev *sza) {
  if (!data || !sza || !data->d_data || !sza->d_data) return;

  unsigned int block = 256;
  unsigned int grid = (unsigned int)((data->size + block - 1) / block);
  cudaEvent_t t0 = NULL, t1 = NULL;
  cudaEventCreate(&t0);
  cudaEventCreate(&t1);
  cudaEventRecord(t0);

  solar_zenith_kernel<<<grid, block>>>(data->d_data, sza->d_data, data->size);

  cudaEventRecord(t1);
  cudaEventSynchronize(t1);
  cudaError_t e = cudaGetLastError();
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, t0, t1);
  cudaEventDestroy(t0);
  cudaEventDestroy(t1);
  if (e != cudaSuccess) {
    LOG_ERROR("apply_solar_zenith_correction_dev: %s", cudaGetErrorString(e));
    return;
  }
  LOG_TIMING_STAGE(TM_CORRECT, ms / 1000.0, "Solar zenith correction (CUDA, device-resident)");
}

/* ---- Rayleigh LUT correction --------------------------------------------- */
__global__ void rayleigh_lut_kernel(float *img, const float *sza,
                                    const float *vza, const float *raa,
                                    const float *redband, RayleighLUTDev lut,
                                    size_t n) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  float original = img[i];
  if (is_nondata_dev(original)) return;

  float theta_s = sza[i];
  // Night/twilight mask (SZA > 88°).
  if (theta_s > 88.0f || is_nondata_dev(theta_s) || theta_s < 0.0f) {
    img[i] = 0.0f;
    return;
  }

  float sza_clipped = theta_s;
  if (sza_clipped > 87.68f) sza_clipped = 87.68f;
  if (sza_clipped < 0.0f) sza_clipped = 0.0f;

  float vza_clipped = vza[i];
  if (vza_clipped > 70.53f) vza_clipped = 70.53f;
  if (vza_clipped < 0.0f) vza_clipped = 0.0f;

  float theta_s_sec = 1.0f / cosf(sza_clipped * HPSV_DEG2RAD);
  float vza_sec = 1.0f / cosf(vza_clipped * HPSV_DEG2RAD);

  float r_corr = get_rayleigh_value_dev(lut, theta_s_sec, vza_sec, raa[i]);

  // Taper for SZA 70–88°.
  if (theta_s > 70.0f) {
    float reduce_factor = 1.0f - (theta_s - 70.0f) / (88.0f - 70.0f);
    if (reduce_factor < 0.0f) reduce_factor = 0.0f;
    r_corr *= reduce_factor;
  }

  // Cloud relaxation with red-band reflectance (>= 0.20).
  if (redband) {
    float rb = redband[i];
    if (!is_nondata_dev(rb) && rb >= 0.20f) {
      r_corr *= 1.0f - (rb - 0.20f) / 0.80f;
      if (r_corr < 0.0f) r_corr = 0.0f;
    }
  }

  float val = original - r_corr;
  if (val < 0.0f) val = 0.0f;
  img[i] = val;
}

extern "C" void luts_rayleigh_correction_dev(DataFDev *img, const DataFDev *sza,
                                             const DataFDev *vza,
                                             const DataFDev *raa,
                                             const RayleighLUTDev *lut,
                                             const DataFDev *redband) {
  if (!img || !sza || !vza || !raa || !lut || !img->d_data || !lut->d_table)
    return;
  if (img->size != sza->size || img->size != vza->size || img->size != raa->size) {
    LOG_ERROR("luts_rayleigh_correction_dev: geometry size mismatch.");
    return;
  }

  const float *d_red = (redband && redband->d_data && redband->size == img->size)
                           ? redband->d_data
                           : NULL;

  unsigned int block = 256;
  unsigned int grid = (unsigned int)((img->size + block - 1) / block);
  cudaEvent_t t0 = NULL, t1 = NULL;
  cudaEventCreate(&t0);
  cudaEventCreate(&t1);
  cudaEventRecord(t0);

  rayleigh_lut_kernel<<<grid, block>>>(img->d_data, sza->d_data, vza->d_data,
                                       raa->d_data, d_red, *lut, img->size);

  cudaEventRecord(t1);
  cudaEventSynchronize(t1);
  cudaError_t e = cudaGetLastError();
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, t0, t1);
  cudaEventDestroy(t0);
  cudaEventDestroy(t1);
  if (e != cudaSuccess) {
    LOG_ERROR("luts_rayleigh_correction_dev: %s", cudaGetErrorString(e));
    return;
  }
  /* Se reporta el canal para poder parear con el [PERF] por canal del CPU
   * ("Rayleigh LUT C%02d (%zu px)", src/rayleigh.c). El conteo NO es el mismo:
   * el CPU cuenta píxeles válidos (salta NonData/noche), mientras que aquí se
   * lanza un hilo por píxel del grid completo, así que se reporta el grid y se
   * dice "grid" para no invitar a comparar los dos números. Contar válidos en
   * device exigiría una reducción que perturbaría lo que se está midiendo. */
  LOG_TIMING_STAGE(TM_CORRECT, ms / 1000.0, "Rayleigh LUT C%02d (%zu px grid, CUDA, device-resident)",
             lut->channel, img->size);
}
