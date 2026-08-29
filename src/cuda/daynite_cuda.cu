/* Pseudocolor nocturno, máscara día/noche y mezcla, residentes en device.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Ports de create_nocturnal_pseudocolor() (src/nocturnal_pseudocolor.c),
 * create_daynight_mask() y blend_images() (src/image.c). Un hilo por píxel.
 * La aritmética se mantiene en el mismo tipo que la CPU en cada paso (double
 * para la geometría solar, float para el color) para no introducir divergencias
 * evitables.
 */

#include "cuda_common.cuh"
#include <stdint.h>

extern "C" {
#include "cuda_daynite.h"
#include "logger.h"
#include "timing.h"
#include "palette.h"
}

#define HPSV_NONDATA_DEV 1.0e+32f
#define DN_PI 3.14159265358979323846

#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                              \
      LOG_ERROR("CUDA error en %s:%d: %s", __FILE__, __LINE__,               \
                cudaGetErrorString(err__));                                  \
      goto cleanup;                                                          \
    }                                                                        \
  } while (0)

/* ---- pseudocolor nocturno -------------------------------------------------
 * La búsqueda del tramo de paleta se deja lineal, igual que en CPU: la tabla
 * tiene 256 entradas y una búsqueda binaria solo daría el mismo resultado si los
 * umbrales fueran estrictamente monótonos, cosa que no conviene asumir aquí. */
__global__ void nocturnal_kernel(const float *temp, unsigned char *out, size_t n,
                                 const PaletteData *pal, const unsigned char *fondo,
                                 unsigned int fondo_bpp, float max_ir_temp) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  size_t po = i * 3;
  unsigned char r = 0, g = 0, b = 0; // NonData -> negro (ver el fix en la ruta CPU)
  float f = temp[i];

  if (!is_nondata_dev(f)) {
    unsigned int t;
    for (t = 0; t < 255; t++)
      if (f >= (float)pal[t].d && f < (float)pal[t + 1].d) break;
    if (t == 255) t = 254; // fuera del último umbral: evita leer pal[256]

    r = (unsigned char)(255 * pal[t].r);
    g = (unsigned char)(255 * pal[t].g);
    b = (unsigned char)(255 * pal[t].b);

    if (fondo && f > max_ir_temp) {
      float w = 1.0f - pal[t].a;
      size_t pf = i * fondo_bpp;
      r = (unsigned char)(r * (1 - w) + w * fondo[pf]);
      g = (unsigned char)(g * (1 - w) + w * fondo[pf + 1]);
      b = (unsigned char)(b * (1 - w) + w * fondo[pf + 2]);
    }
  }

  out[po] = r;
  out[po + 1] = g;
  out[po + 2] = b;
}

extern "C" bool create_nocturnal_pseudocolor_dev(const DataFDev *temp,
                                                 const unsigned char *d_fondo,
                                                 unsigned int fondo_bpp,
                                                 unsigned char **d_out) {
  if (!temp || !temp->d_data || !d_out) return false;
  *d_out = NULL;

  const float max_ir_temp = 263.15f; // igual que la ruta CPU (~-10°C)
  size_t n = temp->size;

  bool ok = false;
  unsigned char *d_img = NULL;
  PaletteData *d_pal = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((n + block - 1) / block);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_img, n * 3));
  CUDA_CHECK(cudaMalloc((void **)&d_pal, 256 * sizeof(PaletteData)));
  CUDA_CHECK(cudaMemcpy(d_pal, atmosrainbow, 256 * sizeof(PaletteData),
                        cudaMemcpyHostToDevice));

  nocturnal_kernel<<<grid, block>>>(temp->d_data, d_img, n, d_pal, d_fondo,
                                    fondo_bpp, max_ir_temp);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_COMPOSE, ms / 1000.0, "Nocturnal pseudocolor (CUDA, device-resident)");
  ok = true;

cleanup:
  if (d_pal) cudaFree(d_pal);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    if (d_img) cudaFree(d_img);
    return false;
  }
  *d_out = d_img;
  return true;
}

/* ---- máscara día/noche ---------------------------------------------------- */
__device__ __forceinline__ double sun_sin_elev_dev(float la, float lo, double sd,
                                                   double cd, double ha_base) {
  double Longitude = lo * (DN_PI / 180.0);
  double Latitude = la * (DN_PI / 180.0);

  double HourAngle = ha_base + Longitude;
  HourAngle = fmod(HourAngle + DN_PI, 2 * DN_PI) - DN_PI;
  if (HourAngle < -DN_PI) HourAngle += 2 * DN_PI;

  double sp = sin(Latitude);
  double cp = sqrt(1 - sp * sp);
  double cH = cos(HourAngle);
  return sp * sd + cp * cd * cH;
}

__global__ void daynight_mask_kernel(const float *temp, const float *la,
                                     const float *lo, unsigned char *mask,
                                     unsigned long long *counts, size_t n,
                                     double sd, double cd, double ha_base,
                                     float max_temp, double se_nite,
                                     double se_twil, double inv_se_range) {
  extern __shared__ unsigned int sh_cnt[]; /* 2 * blockDim: day, nite */
  unsigned int *sh_day = sh_cnt;
  unsigned int *sh_nite = sh_cnt + blockDim.x;

  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  unsigned int my_day = 0, my_nite = 0;

  if (i < n) {
    float t = temp[i];
    float w;
    if (t < max_temp) {
      // Nubes altas y frías se clasifican como noche sin mirar la geometría.
      w = 1.0f;
      my_nite = 1;
    } else {
      double se0 = sun_sin_elev_dev(la[i], lo[i], sd, cd, ha_base);
      if (se0 < se_nite) {
        w = 1.0f;
        my_nite = 1;
      } else if (se0 < se_twil) {
        w = (float)(1.0 - (se0 - se_nite) * inv_se_range);
        if (w >= 0.5f) my_nite = 1; else my_day = 1;
      } else {
        w = 0.0f;
        my_day = 1;
      }
    }
    mask[i] = (unsigned char)(255 * w);
  }

  sh_day[threadIdx.x] = my_day;
  sh_nite[threadIdx.x] = my_nite;
  __syncthreads();
  for (unsigned int s = blockDim.x / 2; s > 0; s >>= 1) {
    if (threadIdx.x < s) {
      sh_day[threadIdx.x] += sh_day[threadIdx.x + s];
      sh_nite[threadIdx.x] += sh_nite[threadIdx.x + s];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    atomicAdd(&counts[0], (unsigned long long)sh_day[0]);
    atomicAdd(&counts[1], (unsigned long long)sh_nite[0]);
  }
}

extern "C" bool create_daynight_mask_dev(const DataFDev *temp, const DataFDev *navla,
                                         const DataFDev *navlo,
                                         const SolarEphemeris_dn *eph,
                                         float max_temp, unsigned char **d_mask,
                                         float *dnratio) {
  if (!temp || !navla || !navlo || !eph || !d_mask || !dnratio) return false;
  if (!temp->d_data || !navla->d_data || !navlo->d_data) return false;
  *d_mask = NULL;

  // Mismos umbrales que la ruta CPU, en sin(elevación) para evitar asin/atan.
  const float terminador = 85.0f, penumbra = 10.0f;
  double se_nite = sin((90.0 - terminador) * DN_PI / 180.0);
  double se_twil = sin((90.0 - terminador + penumbra) * DN_PI / 180.0);
  double inv_se_range = 1.0 / (se_twil - se_nite);

  size_t n = temp->size;
  bool ok = false;
  unsigned char *d_m = NULL;
  unsigned long long *d_counts = NULL;
  unsigned long long counts[2] = {0, 0};
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((n + block - 1) / block);
  size_t shbytes = 2 * block * sizeof(unsigned int);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_m, n));
  CUDA_CHECK(cudaMalloc((void **)&d_counts, 2 * sizeof(unsigned long long)));
  CUDA_CHECK(cudaMemset(d_counts, 0, 2 * sizeof(unsigned long long)));

  daynight_mask_kernel<<<grid, block, shbytes>>>(
      temp->d_data, navla->d_data, navlo->d_data, d_m, d_counts, n, eph->sd,
      eph->cd, eph->hour_angle_base, max_temp, se_nite, se_twil, inv_se_range);
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaMemcpy(counts, d_counts, 2 * sizeof(unsigned long long),
                        cudaMemcpyDeviceToHost));

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_COMPOSE, ms / 1000.0, "Day/night mask (CUDA, device-resident)");
  // Misma fórmula que la CPU, incluido el caso degenerado sin píxeles nocturnos.
  *dnratio = (counts[1] == 0) ? 100.0f : (float)(100.0 * (double)counts[0] / (double)n);
  ok = true;

cleanup:
  if (d_counts) cudaFree(d_counts);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    if (d_m) cudaFree(d_m);
    return false;
  }
  *d_mask = d_m;
  return true;
}

/* ---- mezcla --------------------------------------------------------------- */
__global__ void blend_kernel(const unsigned char *bg, const unsigned char *fg,
                             const unsigned char *mask, unsigned char *out,
                             size_t npx) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= npx) return;
  size_t p = i * 3;
  float w = mask[i] / 255.0f;
  out[p] = (unsigned char)(w * bg[p] + (1 - w) * fg[p]);
  out[p + 1] = (unsigned char)(w * bg[p + 1] + (1 - w) * fg[p + 1]);
  out[p + 2] = (unsigned char)(w * bg[p + 2] + (1 - w) * fg[p + 2]);
}

extern "C" bool blend_images_dev(const unsigned char *d_bg, const unsigned char *d_fg,
                                 const unsigned char *d_mask, unsigned int width,
                                 unsigned int height, unsigned char **d_out) {
  if (!d_bg || !d_fg || !d_mask || !d_out) return false;
  *d_out = NULL;

  size_t npx = (size_t)width * height;
  bool ok = false;
  unsigned char *d_o = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((npx + block - 1) / block);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_o, npx * 3));
  blend_kernel<<<grid, block>>>(d_bg, d_fg, d_mask, d_o, npx);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_COMPOSE, ms / 1000.0, "Image blend (CUDA, device-resident)");
  ok = true;

cleanup:
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    if (d_o) cudaFree(d_o);
    return false;
  }
  *d_out = d_o;
  return true;
}
