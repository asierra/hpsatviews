/* Device-resident viewing geometry (solar + satellite + relative azimuth).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Port de sun_angles_from_ephemeris / compute_satellite_view_angles /
 * compute_relative_azimuth (src/reader_nc.c). La parte solo-tiempo de la
 * geometría solar (efeméride) se precomputa una vez en host y llega como
 * escalares (sd, cd, ha_base), evitando ~20 trig por píxel. Todo en double para
 * casar con la ruta CPU. Un hilo por píxel.
 */

#include "cuda_common.cuh"
#include <stdint.h>

extern "C" {
#include "cuda_nav.h"
#include "logger.h"
}

#define HPSV_NONDATA_DEV 1.0e+32f
#define HPSV_PI 3.14159265358979323846
#define HPSV_PI2 (2.0 * HPSV_PI)
#define HPSV_PIM 1.57079632679489661923

#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                             \
      LOG_ERROR("CUDA error en %s:%d: %s", __FILE__, __LINE__,               \
                cudaGetErrorString(err__));                                  \
      goto cleanup;                                                          \
    }                                                                        \
  } while (0)

/* ---- solar (port de sun_angles_from_ephemeris) --------------------------- */
__global__ void solar_kernel(const float *la, const float *lo, float *sza,
                             float *saa, size_t n, double sd, double cd,
                             double ha_base) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  float laf = la[i], lof = lo[i];
  if (is_nondata_dev(laf) || is_nondata_dev(lof)) {
    sza[i] = HPSV_NONDATA_DEV;
    saa[i] = HPSV_NONDATA_DEV;
    return;
  }

  double Longitude = (double)lof * HPSV_PI / 180.0;
  double Latitude = (double)laf * HPSV_PI / 180.0;

  double HourAngle = ha_base + Longitude;
  HourAngle = fmod(HourAngle + HPSV_PI, HPSV_PI2) - HPSV_PI;
  if (HourAngle < -HPSV_PI) HourAngle += HPSV_PI2;

  double sp = sin(Latitude);
  double cp = sqrt(1.0 - sp * sp);
  double sH = sin(HourAngle);
  double cH = cos(HourAngle);
  double se0 = sp * sd + cp * cd * cH;
  double ep = asin(se0) - 4.26e-5 * sqrt(1.0 - se0 * se0);
  double Azimuth = atan2(sH, cH * sp - sd * cp / cd);

  double De = 0.0;
  if (ep > 0.0)
    De = (0.08422 * 1.0) / ((273.0 + 0.0) * tan(ep + 0.003138 / (ep + 0.08919)));
  double Zenith = HPSV_PIM - ep - De;

  sza[i] = (float)(Zenith * 180.0 / HPSV_PI);
  saa[i] = (float)(Azimuth * 180.0 / HPSV_PI);
}

/* ---- satellite (port de compute_satellite_view_angles) ------------------- */
__global__ void satellite_kernel(const float *la, const float *lo, float *vza,
                                 float *vaa, size_t n, float sat_lon,
                                 float sat_height_m) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  float laf = la[i], lof = lo[i];
  if (is_nondata_dev(laf) || is_nondata_dev(lof)) {
    vza[i] = HPSV_NONDATA_DEV;
    vaa[i] = HPSV_NONDATA_DEV;
    return;
  }

  const double a = 6378137.0;
  const double f = 1.0 / 298.257223563;
  double lat_rad = (double)laf * HPSV_PI / 180.0;
  double lon_rad = (double)lof * HPSV_PI / 180.0;
  double sat_lon_rad = (double)sat_lon * HPSV_PI / 180.0;

  double N = a / sqrt(1.0 - (2.0 * f - f * f) * sin(lat_rad) * sin(lat_rad));
  double x_pixel = N * cos(lat_rad) * cos(lon_rad);
  double y_pixel = N * cos(lat_rad) * sin(lon_rad);
  double z_pixel = N * (1.0 - (2.0 * f - f * f)) * sin(lat_rad);

  double sat_radius = a + (double)sat_height_m;
  double x_sat = sat_radius * cos(sat_lon_rad);
  double y_sat = sat_radius * sin(sat_lon_rad);

  double dx = x_pixel - x_sat;
  double dy = y_pixel - y_sat;
  double dz = z_pixel - 0.0;
  double dist = sqrt(dx * dx + dy * dy + dz * dz);
  dx /= dist; dy /= dist; dz /= dist;

  double n_len = sqrt(x_pixel * x_pixel + y_pixel * y_pixel + z_pixel * z_pixel);
  double nx = x_pixel / n_len;
  double ny = y_pixel / n_len;
  double nz = z_pixel / n_len;

  double cos_vza = -(dx * nx + dy * ny + dz * nz);
  double vza_deg = acos(fmax(-1.0, fmin(1.0, cos_vza))) * 180.0 / HPSV_PI;

  double east_x = -sin(lon_rad), east_y = cos(lon_rad);
  double north_x = -sin(lat_rad) * cos(lon_rad);
  double north_y = -sin(lat_rad) * sin(lon_rad);
  double north_z = cos(lat_rad);

  double view_east = dx * east_x + dy * east_y;
  double view_north = dx * north_x + dy * north_y + dz * north_z;
  double vaa_deg = atan2(view_east, view_north) * 180.0 / HPSV_PI;

  vza[i] = (float)vza_deg;
  vaa[i] = (float)vaa_deg;
}

/* ---- relative azimuth (port de compute_relative_azimuth) ----------------- */
__global__ void relaz_kernel(const float *saa, const float *vaa, float *raa,
                             size_t n) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  float sa = saa[i], va = vaa[i];
  if (is_nondata_dev(sa) || is_nondata_dev(va)) {
    raa[i] = HPSV_NONDATA_DEV;
  } else {
    float diff = fabsf(sa - va);
    if (diff > 180.0f) diff = 360.0f - diff;
    raa[i] = diff;
  }
}

extern "C" bool compute_rayleigh_nav_dev(const DataFDev *navla,
                                         const DataFDev *navlo, double sd,
                                         double cd, double ha_base,
                                         float sat_lon, float sat_height_m,
                                         DataFDev *sza_out, DataFDev *vza_out,
                                         DataFDev *raa_out) {
  if (!navla || !navlo || !navla->d_data || !navlo->d_data) return false;
  if (navla->size != navlo->size) {
    LOG_ERROR("compute_rayleigh_nav_dev: lat/lon size mismatch.");
    return false;
  }

  size_t n = navla->size;
  unsigned int w = navla->width, h = navla->height;

  DataFDev sza = dataf_dev_alloc(w, h);
  DataFDev saa = dataf_dev_alloc(w, h);
  DataFDev vza = dataf_dev_alloc(w, h);
  DataFDev vaa = dataf_dev_alloc(w, h);
  DataFDev raa = dataf_dev_alloc(w, h);

  bool ok = false;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((n + block - 1) / block);

  if (!sza.d_data || !saa.d_data || !vza.d_data || !vaa.d_data || !raa.d_data)
    goto cleanup;

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  solar_kernel<<<grid, block>>>(navla->d_data, navlo->d_data, sza.d_data,
                                saa.d_data, n, sd, cd, ha_base);
  CUDA_CHECK(cudaGetLastError());
  satellite_kernel<<<grid, block>>>(navla->d_data, navlo->d_data, vza.d_data,
                                    vaa.d_data, n, sat_lon, sat_height_m);
  CUDA_CHECK(cudaGetLastError());
  relaz_kernel<<<grid, block>>>(saa.d_data, vaa.d_data, raa.d_data, n);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING(ms / 1000.0, "Navigation (CUDA, device-resident: solar+sat+raa)");
  ok = true;

cleanup:
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  dataf_dev_destroy(&saa);
  dataf_dev_destroy(&vaa);
  if (!ok) {
    dataf_dev_destroy(&sza);
    dataf_dev_destroy(&vza);
    dataf_dev_destroy(&raa);
    return false;
  }
  *sza_out = sza;
  *vza_out = vza;
  *raa_out = raa;
  return true;
}
