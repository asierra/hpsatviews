/* Device fixed-grid -> geographic reprojection (inverse scan-angle gather).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Port directo del cuerpo del loop de reproject_image_analytical()
 * (src/reprojection.c), reusando reproject_build_plan() para el setup. Un hilo
 * por píxel de salida; todo en double para casar con la ruta CPU. La fuente
 * (imagen uint8 compuesta) se sube a device y la salida se baja.
 */

#include <cuda_runtime.h>
#include <stdint.h>

extern "C" {
#include "cuda_reproject.h"
#include "reprojection.h"
#include "logger.h"
}

#define HPSV_PI 3.14159265358979323846

#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                             \
      LOG_ERROR("CUDA error en %s:%d: %s", __FILE__, __LINE__,               \
                cudaGetErrorString(err__));                                  \
      goto cleanup;                                                          \
    }                                                                        \
  } while (0)

__global__ void reproject_kernel(const unsigned char *src, unsigned char *out,
                                 ReprojPlan p, const unsigned char *nodata,
                                 bool has_nodata) {
  unsigned int ox = blockIdx.x * blockDim.x + threadIdx.x;
  unsigned int oy = blockIdx.y * blockDim.y + threadIdx.y;
  if (ox >= p.width || oy >= p.height) return;

  unsigned int bpp = p.bpp;
  size_t dst_idx = ((size_t)oy * p.width + ox) * bpp;

  double lon_deg = p.target_lon_min + ((double)ox + 0.5) * p.deg_per_px_lon;
  double lat_deg = p.target_lat_max - ((double)oy + 0.5) * p.deg_per_px_lat;
  double phi = lat_deg * (HPSV_PI / 180.0);
  double lambda = lon_deg * (HPSV_PI / 180.0);

  double phi_c = atan(p.b2_over_a2 * tan(phi));
  double cos_phi_c = cos(phi_c);
  double sin_phi_c = sin(phi_c);

  double r_c = p.b / sqrt(1.0 - p.e2 * cos_phi_c * cos_phi_c);

  double d_lambda = lambda - p.lambda0;
  double s_x = p.H - r_c * cos_phi_c * cos(d_lambda);
  double s_y = -r_c * cos_phi_c * sin(d_lambda);
  double s_z = r_c * sin_phi_c;

  if (p.H * (p.H - s_x) < s_y * s_y + p.a2_over_b2 * s_z * s_z) {
    if (has_nodata)
      for (unsigned int c = 0; c < bpp; c++) out[dst_idx + c] = nodata[c];
    return;
  }

  double s_n = sqrt(s_x * s_x + s_y * s_y + s_z * s_z);
  double x_rad = asin(-s_y / s_n);
  double y_rad = atan2(s_z, s_x);

  double col = (x_rad - p.safe_gt[0]) / p.safe_gt[1];
  double row = (y_rad - p.safe_gt[3]) / p.safe_gt[5];

  if (col < 0.0 || col >= (double)(p.src_w - 1) || row < 0.0 ||
      row >= (double)(p.src_h - 1)) {
    if (has_nodata)
      for (unsigned int c = 0; c < bpp; c++) out[dst_idx + c] = nodata[c];
    return;
  }

  if (bpp == 1) {
    int c_nn = (int)(col + 0.5);
    int r_nn = (int)(row + 0.5);
    out[dst_idx] = src[((size_t)r_nn * p.src_w + (size_t)c_nn) * bpp];
  } else {
    int c0 = (int)col;
    int r0 = (int)row;
    double dc = col - c0;
    double dr = row - r0;
    int c1 = c0 + 1;
    int r1 = r0 + 1;

    double w00 = (1.0 - dc) * (1.0 - dr);
    double w10 = dc * (1.0 - dr);
    double w01 = (1.0 - dc) * dr;
    double w11 = dc * dr;

    size_t i00 = ((size_t)r0 * p.src_w + (size_t)c0) * bpp;
    size_t i10 = ((size_t)r0 * p.src_w + (size_t)c1) * bpp;
    size_t i01 = ((size_t)r1 * p.src_w + (size_t)c0) * bpp;
    size_t i11 = ((size_t)r1 * p.src_w + (size_t)c1) * bpp;

    for (unsigned int ch = 0; ch < bpp; ch++) {
      double val = w00 * src[i00 + ch] + w10 * src[i10 + ch] +
                   w01 * src[i01 + ch] + w11 * src[i11 + ch];
      int ival = (int)(val + 0.5);
      out[dst_idx + ch] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
    }
  }
}

extern "C" ImageData reproject_image_analytical_cuda(
    const ImageData *src_image, const DataNC *data_nc, float lat_min,
    float lat_max, float lon_min, float lon_max, float native_resolution_km,
    const float *clip_coords, const unsigned char *nodata_pixel) {

  ReprojPlan p = reproject_build_plan(src_image, data_nc, lat_min, lat_max,
                                      lon_min, lon_max, native_resolution_km,
                                      clip_coords);
  if (p.width == 0) return image_create(0, 0, 0);

  LOG_INFO("Analytic reprojection (CUDA): %ux%u (bpp:%u) -> %ux%u",
           src_image->width, src_image->height, src_image->bpp, p.width,
           p.height);

  ImageData geo_image = image_create(p.width, p.height, p.bpp);
  if (!geo_image.data) {
    LOG_FATAL("Memory allocation failed for destination geographic image (CUDA).");
    return geo_image;
  }

  size_t src_bytes = (size_t)src_image->width * src_image->height * src_image->bpp;
  size_t out_bytes = (size_t)p.width * p.height * p.bpp;

  bool ok = false;
  unsigned char *d_src = NULL, *d_out = NULL, *d_nodata = NULL;
  cudaEvent_t t0 = NULL, t1 = NULL;
  float ms = 0.0f;
  dim3 block(16, 16);
  dim3 grid((p.width + block.x - 1) / block.x,
            (p.height + block.y - 1) / block.y);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventRecord(t0));

  CUDA_CHECK(cudaMalloc((void **)&d_src, src_bytes));
  CUDA_CHECK(cudaMalloc((void **)&d_out, out_bytes));
  CUDA_CHECK(cudaMemcpy(d_src, src_image->data, src_bytes, cudaMemcpyHostToDevice));
  // Discarded pixels without a nodata pattern stay 0 (matches the CPU memset).
  CUDA_CHECK(cudaMemset(d_out, 0, out_bytes));
  if (nodata_pixel) {
    CUDA_CHECK(cudaMalloc((void **)&d_nodata, p.bpp));
    CUDA_CHECK(cudaMemcpy(d_nodata, nodata_pixel, p.bpp, cudaMemcpyHostToDevice));
  }

  reproject_kernel<<<grid, block>>>(d_src, d_out, p, d_nodata,
                                    nodata_pixel != NULL);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaMemcpy(geo_image.data, d_out, out_bytes, cudaMemcpyDeviceToHost));

  CUDA_CHECK(cudaEventRecord(t1));
  CUDA_CHECK(cudaEventSynchronize(t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
  LOG_TIMING(ms / 1000.0, "Analytic reprojection (CUDA, incl. transferencias)");
  ok = true;

cleanup:
  if (d_src) cudaFree(d_src);
  if (d_out) cudaFree(d_out);
  if (d_nodata) cudaFree(d_nodata);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (!ok) {
    free(geo_image.data);
    return image_create(0, 0, 0);
  }
  return geo_image;
}
