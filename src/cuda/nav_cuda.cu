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
#include "timing.h"
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
  /* NO se llama "Navigation": ese nombre ya lo usa el [PERF] de la malla
   * lat/lon (reader_nc.c), que corre en CPU también bajo --cuda y por tanto
   * aparece en el mismo log. Parear esos dos por el nombre daría un speedup
   * falso; la contraparte real de este kernel son los [PERF] "Solar geometry" +
   * "Satellite geometry" del CPU (el RAA del CPU va sin medir dentro de
   * compute_relative_azimuth). */
  LOG_TIMING_STAGE(TM_GEOM, ms / 1000.0,
             "Solar+satellite geometry (CUDA, device-resident: solar+sat+raa)");
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

/* ---- lat/lon en device (port del bucle de compute_navigation_nc) ----------
 * Misma aritmética en double que la ruta CPU, incluido el precómputo de
 * sin/cos por columna y por fila: cada hilo lee 4 dobles en vez de hacer 4
 * llamadas trigonométricas. Eso evita además que la diferencia de 1 ULP entre
 * las trig de CUDA y las de glibc se aplique de forma distinta por píxel.
 *
 * Además del grid, reduce el mínimo/máximo de lat y lon, que es lo único que la
 * ruta CPU necesitaba de vuelta en host (fija la extensión del reproyectado).
 * La reducción se hace en double, como en CPU, y solo sobre píxeles dentro del
 * disco; los de fuera quedan en NonData y no participan. */
__global__ void sincos_axis_kernel(const double *ang, double *sn, double *cs,
                                   size_t n) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  sn[i] = sin(ang[i]);
  cs[i] = cos(ang[i]);
}

__global__ void latlon_kernel(const double *snx, const double *csx,
                              const double *sny, const double *csy,
                              float *la_out, float *lo_out, double *partials,
                              unsigned int width, unsigned int height,
                              double H, double lambda_0, double sm_maj2,
                              double sm_min2, double ratio, double H2_maj2,
                              double rad2deg) {
  extern __shared__ double sh[]; /* 4 * blockDim: lamin, lamax, lomin, lomax */
  double *sh_lamin = sh;
  double *sh_lamax = sh + blockDim.x;
  double *sh_lomin = sh + 2 * blockDim.x;
  double *sh_lomax = sh + 3 * blockDim.x;

  size_t n = (size_t)width * height;
  size_t k = (size_t)blockIdx.x * blockDim.x + threadIdx.x;

  double lamin = 1e10, lamax = -1e10, lomin = 1e10, lomax = -1e10;

  if (k < n) {
    size_t j = k / width, i = k % width;
    double sny_j = sny[j], csy_j = csy[j];
    double csy2 = csy_j * csy_j, rat_sny2 = ratio * sny_j * sny_j;
    double snx_i = snx[i], csx_i = csx[i];

    double a = snx_i * snx_i + csx_i * csx_i * (csy2 + rat_sny2);
    double b = -2.0 * H * csx_i * csy_j;
    double disc = b * b - 4.0 * a * H2_maj2;
    if (disc < 0.0) {
      la_out[k] = HPSV_NONDATA_DEV;
      lo_out[k] = HPSV_NONDATA_DEV;
    } else {
      double rs = (-b - sqrt(disc)) / (2.0 * a);
      double px = rs * csx_i * csy_j;
      double py = -rs * snx_i;
      double pz = rs * csx_i * sny_j;
      double la = atan2(sm_maj2 * pz,
                        sm_min2 * sqrt((H - px) * (H - px) + py * py)) * rad2deg;
      double lo = (lambda_0 - atan2(py, H - px)) * rad2deg;
      la_out[k] = (float)la;
      lo_out[k] = (float)lo;
      lamin = lamax = la;
      lomin = lomax = lo;
    }
  }

  sh_lamin[threadIdx.x] = lamin;
  sh_lamax[threadIdx.x] = lamax;
  sh_lomin[threadIdx.x] = lomin;
  sh_lomax[threadIdx.x] = lomax;
  __syncthreads();

  for (unsigned int s = blockDim.x / 2; s > 0; s >>= 1) {
    if (threadIdx.x < s) {
      sh_lamin[threadIdx.x] = fmin(sh_lamin[threadIdx.x], sh_lamin[threadIdx.x + s]);
      sh_lamax[threadIdx.x] = fmax(sh_lamax[threadIdx.x], sh_lamax[threadIdx.x + s]);
      sh_lomin[threadIdx.x] = fmin(sh_lomin[threadIdx.x], sh_lomin[threadIdx.x + s]);
      sh_lomax[threadIdx.x] = fmax(sh_lomax[threadIdx.x], sh_lomax[threadIdx.x + s]);
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    partials[4 * blockIdx.x + 0] = sh_lamin[0];
    partials[4 * blockIdx.x + 1] = sh_lamax[0];
    partials[4 * blockIdx.x + 2] = sh_lomin[0];
    partials[4 * blockIdx.x + 3] = sh_lomax[0];
  }
}

extern "C" bool compute_navigation_dev(const NavPlan *plan, DataFDev *lat_out,
                                       DataFDev *lon_out, float *lat_min,
                                       float *lat_max, float *lon_min,
                                       float *lon_max) {
  if (!plan || !plan->x_rad || !plan->y_rad || !lat_out || !lon_out) return false;

  const double rad2deg = 180.0 / HPSV_PI;
  unsigned int w = (unsigned int)plan->width, h = (unsigned int)plan->height;
  size_t n = (size_t)w * h;

  DataFDev la = dataf_dev_alloc(w, h);
  DataFDev lo = dataf_dev_alloc(w, h);

  bool ok = false;
  double *d_x = NULL, *d_y = NULL, *d_snx = NULL, *d_csx = NULL;
  double *d_sny = NULL, *d_csy = NULL, *d_partials = NULL, *partials = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  unsigned int block = 256;
  unsigned int grid = (unsigned int)((n + block - 1) / block);
  size_t shbytes = 4 * block * sizeof(double);

  if (!la.d_data || !lo.d_data) goto cleanup;

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  /* Los ejes son diminutos (w+h dobles) comparados con el grid. */
  CUDA_CHECK(cudaMalloc((void **)&d_x, w * sizeof(double)));
  CUDA_CHECK(cudaMalloc((void **)&d_y, h * sizeof(double)));
  CUDA_CHECK(cudaMalloc((void **)&d_snx, w * sizeof(double)));
  CUDA_CHECK(cudaMalloc((void **)&d_csx, w * sizeof(double)));
  CUDA_CHECK(cudaMalloc((void **)&d_sny, h * sizeof(double)));
  CUDA_CHECK(cudaMalloc((void **)&d_csy, h * sizeof(double)));
  CUDA_CHECK(cudaMalloc((void **)&d_partials, 4 * grid * sizeof(double)));
  CUDA_CHECK(cudaMemcpy(d_x, plan->x_rad, w * sizeof(double), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_y, plan->y_rad, h * sizeof(double), cudaMemcpyHostToDevice));

  sincos_axis_kernel<<<(w + 255) / 256, 256>>>(d_x, d_snx, d_csx, w);
  CUDA_CHECK(cudaGetLastError());
  sincos_axis_kernel<<<(h + 255) / 256, 256>>>(d_y, d_sny, d_csy, h);
  CUDA_CHECK(cudaGetLastError());

  latlon_kernel<<<grid, block, shbytes>>>(
      d_snx, d_csx, d_sny, d_csy, la.d_data, lo.d_data, d_partials, w, h,
      plan->H, plan->lambda_0, plan->sm_maj * plan->sm_maj,
      plan->sm_min * plan->sm_min,
      (plan->sm_maj * plan->sm_maj) / (plan->sm_min * plan->sm_min),
      plan->H * plan->H - plan->sm_maj * plan->sm_maj, rad2deg);
  CUDA_CHECK(cudaGetLastError());

  /* Cerrar la reducción en host: son 4 dobles por bloque (unos pocos MB en un
   * disco completo), así que no vale la pena un segundo kernel. */
  partials = (double *)malloc(4 * grid * sizeof(double));
  if (!partials) goto cleanup;
  CUDA_CHECK(cudaMemcpy(partials, d_partials, 4 * grid * sizeof(double),
                        cudaMemcpyDeviceToHost));

  {
    double lamin = 1e10, lamax = -1e10, lomin = 1e10, lomax = -1e10;
    for (unsigned int i = 0; i < grid; i++) {
      if (partials[4 * i + 0] < lamin) lamin = partials[4 * i + 0];
      if (partials[4 * i + 1] > lamax) lamax = partials[4 * i + 1];
      if (partials[4 * i + 2] < lomin) lomin = partials[4 * i + 2];
      if (partials[4 * i + 3] > lomax) lomax = partials[4 * i + 3];
    }
    if (lamin > 1e9) { /* ningún píxel dentro del disco */
      *lat_min = -90.0f; *lat_max = 90.0f;
      *lon_min = -180.0f; *lon_max = 180.0f;
      LOG_WARN("No valid navigation pixels in compute_navigation_dev; using default extents.");
    } else {
      *lat_min = (float)lamin; *lat_max = (float)lamax;
      *lon_min = (float)lomin; *lon_max = (float)lomax;
    }
  }

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING_STAGE(TM_NAV, ms / 1000.0, "Navigation lat/lon (CUDA, device-resident, %ux%u)", w, h);
  ok = true;

cleanup:
  free(partials);
  if (d_x) cudaFree(d_x);
  if (d_y) cudaFree(d_y);
  if (d_snx) cudaFree(d_snx);
  if (d_csx) cudaFree(d_csx);
  if (d_sny) cudaFree(d_sny);
  if (d_csy) cudaFree(d_csy);
  if (d_partials) cudaFree(d_partials);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    dataf_dev_destroy(&la);
    dataf_dev_destroy(&lo);
    return false;
  }
  *lat_out = la;
  *lon_out = lo;
  return true;
}
