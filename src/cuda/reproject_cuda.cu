/* Device fixed-grid -> geographic reprojection (inverse scan-angle gather).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Port directo del cuerpo del loop de reproject_image_analytical()
 * (src/reprojection.c), reusando reproject_build_plan() para el setup. Un hilo
 * por píxel de salida, en float con respaldo en double (ver
 * reproject_float_kernel). La fuente (imagen uint8 compuesta) se sube a device
 * y la salida se baja.
 */

#include <cuda_runtime.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {
#include "cuda_reproject.h"
#include "reprojection.h"
#include "logger.h"
#include "timing.h"
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

/* Inverso escala-angulo -> (col,row) de fuente, en T. Devuelve false si el
 * punto no es visible desde el satelite. En *margin deja la holgura de la prueba
 * de visibilidad relativa a H^2, para que el llamador sepa si la decision quedo
 * al filo. */
template <typename T>
__device__ __forceinline__ bool reproject_inverse(const ReprojPlan &p, unsigned int ox,
                                                  unsigned int oy, T *col, T *row,
                                                  T *margin) {
  const T H = (T)p.H;
  T lon_deg = (T)p.target_lon_min + ((T)ox + (T)0.5) * (T)p.deg_per_px_lon;
  T lat_deg = (T)p.target_lat_max - ((T)oy + (T)0.5) * (T)p.deg_per_px_lat;
  T phi = lat_deg * (T)(HPSV_PI / 180.0);
  T lambda = lon_deg * (T)(HPSV_PI / 180.0);

  T phi_c = atan((T)p.b2_over_a2 * tan(phi));
  T cos_phi_c = cos(phi_c);
  T sin_phi_c = sin(phi_c);

  T r_c = (T)p.b / sqrt((T)1 - (T)p.e2 * cos_phi_c * cos_phi_c);

  T d_lambda = lambda - (T)p.lambda0;
  T s_x = H - r_c * cos_phi_c * cos(d_lambda);
  T s_y = -r_c * cos_phi_c * sin(d_lambda);
  T s_z = r_c * sin_phi_c;

  T vis = s_y * s_y + (T)p.a2_over_b2 * s_z * s_z - H * (H - s_x);
  *margin = vis / (H * H);
  if (vis > (T)0) return false;

  T s_n = sqrt(s_x * s_x + s_y * s_y + s_z * s_z);
  T x_rad = asin(-s_y / s_n);
  T y_rad = atan2(s_z, s_x);

  *col = (x_rad - (T)p.safe_gt[0]) / (T)p.safe_gt[1];
  *row = (y_rad - (T)p.safe_gt[3]) / (T)p.safe_gt[5];
  return true;
}

/* Muestreo en (col,row) ya validados: vecino mas cercano con bpp 1, bilineal si
 * no. Mismo redondeo que reproject_image_analytical(). */
template <typename T>
__device__ __forceinline__ void reproject_sample(const unsigned char *src, unsigned char *out,
                                                 const ReprojPlan &p, size_t dst_idx,
                                                 T col, T row) {
  unsigned int bpp = p.bpp;
  if (bpp == 1) {
    int c_nn = (int)(col + (T)0.5);
    int r_nn = (int)(row + (T)0.5);
    out[dst_idx] = src[((size_t)r_nn * p.src_w + (size_t)c_nn) * bpp];
    return;
  }
  int c0 = (int)col;
  int r0 = (int)row;
  T dc = col - c0;
  T dr = row - r0;
  int c1 = c0 + 1;
  int r1 = r0 + 1;

  T w00 = ((T)1 - dc) * ((T)1 - dr);
  T w10 = dc * ((T)1 - dr);
  T w01 = ((T)1 - dc) * dr;
  T w11 = dc * dr;

  size_t i00 = ((size_t)r0 * p.src_w + (size_t)c0) * bpp;
  size_t i10 = ((size_t)r0 * p.src_w + (size_t)c1) * bpp;
  size_t i01 = ((size_t)r1 * p.src_w + (size_t)c0) * bpp;
  size_t i11 = ((size_t)r1 * p.src_w + (size_t)c1) * bpp;

  for (unsigned int ch = 0; ch < bpp; ch++) {
    T val = w00 * src[i00 + ch] + w10 * src[i10 + ch] +
            w01 * src[i01 + ch] + w11 * src[i11 + ch];
    int ival = (int)(val + (T)0.5);
    out[dst_idx + ch] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
  }
}

/* Pixel (ox,oy) completo en T: inverso, pruebas de borde y muestreo. */
template <typename T>
__device__ __forceinline__ void reproject_pixel(const unsigned char *src, unsigned char *out,
                                                const ReprojPlan &p, const unsigned char *nodata,
                                                bool has_nodata, unsigned int ox,
                                                unsigned int oy) {
  size_t dst_idx = ((size_t)oy * p.width + ox) * p.bpp;
  T col = 0, row = 0, m = 0;
  bool on = reproject_inverse<T>(p, ox, oy, &col, &row, &m);
  if (!on || col < (T)0 || col >= (T)(p.src_w - 1) || row < (T)0 ||
      row >= (T)(p.src_h - 1)) {
    if (has_nodata)
      for (unsigned int c = 0; c < p.bpp; c++) out[dst_idx + c] = nodata[c];
    return;
  }
  reproject_sample<T>(src, out, p, dst_idx, col, row);
}

/* La reproyeccion era el unico kernel de la ruta de produccion que seguia en
 * double, y en una T4 (FP64 1:32) tardaba mas que la CPU de 64 hilos del mismo
 * servidor (0.31-0.33 s contra 0.26 s, disco completo -B, 2026-09-18).
 *
 * Se calcula en float, y en double solo el pixel cuya decision quedo al filo de
 * un umbral. El valor en float se equivoca en <= 0.004 px de fuente
 * (reproduction/float_nav_error.c), lo que en el bilineal cuesta a lo mas un DN
 * de redondeo; pero las decisiones con umbral no son continuas. Medido en un
 * disco completo a 2 km sin este respaldo: en el limbo float y double discrepaban
 * sobre si el punto es visible o cae dentro de la fuente (8 pixeles en airmass,
 * hasta 195 DN, porque de un lado queda el relleno), y en gray el vecino mas
 * cercano elegia al de al lado en 13 000 pixeles cuya coordenada caia a medio
 * pixel (hasta 161 DN). Con el respaldo gray sale identico a la CPU.
 *
 * Son dos pasadas y no un if en el mismo kernel: con el if, un solo pixel al
 * filo hace esperar a su warp entero por la rama double, y con ~4% de pixeles
 * al filo en gray eso tocaba a ~70% de los warps (0.093 s contra 0.125 s en
 * double puro, RTX 5060 Ti). Encolados, se paga en proporcion a los pixeles.
 *
 * HPSV_REPROJECT_FP64=1 hace todo en double, para el A/B. */
__global__ void reproject_float_kernel(const unsigned char *src, unsigned char *out,
                                       ReprojPlan p, const unsigned char *nodata,
                                       bool has_nodata, unsigned int *queue,
                                       unsigned int *queue_len, unsigned int queue_cap) {
  unsigned int ox = blockIdx.x * blockDim.x + threadIdx.x;
  unsigned int oy = blockIdx.y * blockDim.y + threadIdx.y;
  if (ox >= p.width || oy >= p.height) return;

  const float kEdge = 0.01f;   /* px: 2.5x el error maximo medido del inverso  */
  const float kVis = 1.0e-6f;  /* holgura de visibilidad relativa a H^2         */

  float col = 0.0f, row = 0.0f, m = 0.0f;
  bool on = reproject_inverse<float>(p, ox, oy, &col, &row, &m);
  bool borderline = fabsf(m) < kVis;
  if (!borderline && on) {
    float wmax = (float)(p.src_w - 1), hmax = (float)(p.src_h - 1);
    borderline = col < kEdge || row < kEdge || fabsf(col - wmax) < kEdge ||
                 fabsf(row - hmax) < kEdge ||
                 (p.bpp == 1 && (fabsf(col - floorf(col) - 0.5f) < kEdge ||
                                 fabsf(row - floorf(row) - 0.5f) < kEdge));
  }
  if (borderline) {
    unsigned int k = atomicAdd(queue_len, 1u);
    if (k < queue_cap) queue[k] = oy * p.width + ox;
    return; /* si la cola se llena, el llamador rehace todo en double */
  }

  size_t dst_idx = ((size_t)oy * p.width + ox) * p.bpp;
  if (!on || col < 0.0f || col >= (float)(p.src_w - 1) || row < 0.0f ||
      row >= (float)(p.src_h - 1)) {
    if (has_nodata)
      for (unsigned int c = 0; c < p.bpp; c++) out[dst_idx + c] = nodata[c];
    return;
  }
  reproject_sample<float>(src, out, p, dst_idx, col, row);
}

/* Segunda pasada en double: los pixeles de la cola, o todos si queue es NULL. */
__global__ void reproject_double_kernel(const unsigned char *src, unsigned char *out,
                                        ReprojPlan p, const unsigned char *nodata,
                                        bool has_nodata, const unsigned int *queue,
                                        size_t n) {
  size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  size_t pix = queue ? (size_t)queue[i] : i;
  reproject_pixel<double>(src, out, p, nodata, has_nodata,
                          (unsigned int)(pix % p.width), (unsigned int)(pix / p.width));
}

extern "C" ImageData reproject_image_analytical_cuda(
    const ImageData *src_image, const DataNC *data_nc, float lat_min,
    float lat_max, float lon_min, float lon_max, float native_resolution_km,
    const float *clip_coords, const unsigned char *nodata_pixel,
    const unsigned char *d_src_image) {

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
  /* d_src_owned solo se usa cuando hay que subir la imagen; si el llamador ya la
   * tiene en device, se apunta a la suya y no se libera aquí. */
  unsigned char *d_src_owned = NULL, *d_out = NULL, *d_nodata = NULL;
  const unsigned char *d_src = d_src_image;
  const bool fp64 = getenv("HPSV_REPROJECT_FP64") != NULL;
  /* Cola de pixeles al filo para la pasada en double. En un disco completo a
   * 2 km con vecino mas cercano son ~4% de la salida; 1/8 deja margen. */
  unsigned int *d_queue = NULL, *d_queue_len = NULL, queue_len = 0;
  const size_t queue_cap = ((size_t)p.width * p.height) / 8 + 1024;
  cudaEvent_t t0 = NULL, t1 = NULL, t2 = NULL, t3 = NULL;
  float ms_in = 0.0f, ms_kernel = 0.0f, ms_out = 0.0f;
  dim3 block(16, 16);
  dim3 grid((p.width + block.x - 1) / block.x,
            (p.height + block.y - 1) / block.y);

  CUDA_CHECK(cudaEventCreate(&t0));
  CUDA_CHECK(cudaEventCreate(&t1));
  CUDA_CHECK(cudaEventCreate(&t2));
  CUDA_CHECK(cudaEventCreate(&t3));
  CUDA_CHECK(cudaEventRecord(t0));

  if (!d_src) {
    CUDA_CHECK(cudaMalloc((void **)&d_src_owned, src_bytes));
    CUDA_CHECK(cudaMemcpy(d_src_owned, src_image->data, src_bytes,
                          cudaMemcpyHostToDevice));
    d_src = d_src_owned;
  }
  CUDA_CHECK(cudaMalloc((void **)&d_out, out_bytes));
  // Discarded pixels without a nodata pattern stay 0 (matches the CPU memset).
  CUDA_CHECK(cudaMemset(d_out, 0, out_bytes));
  if (nodata_pixel) {
    CUDA_CHECK(cudaMalloc((void **)&d_nodata, p.bpp));
    CUDA_CHECK(cudaMemcpy(d_nodata, nodata_pixel, p.bpp, cudaMemcpyHostToDevice));
  }

  if (!fp64) {
    CUDA_CHECK(cudaMalloc((void **)&d_queue, queue_cap * sizeof(unsigned int)));
    CUDA_CHECK(cudaMalloc((void **)&d_queue_len, sizeof(unsigned int)));
    CUDA_CHECK(cudaMemset(d_queue_len, 0, sizeof(unsigned int)));
  }

  CUDA_CHECK(cudaEventRecord(t1));
  {
    const bool nd = nodata_pixel != NULL;
    const unsigned int b1 = 256;
    if (!fp64) {
      reproject_float_kernel<<<grid, block>>>(d_src, d_out, p, d_nodata, nd, d_queue,
                                              d_queue_len, (unsigned int)queue_cap);
      CUDA_CHECK(cudaGetLastError());
      CUDA_CHECK(cudaMemcpy(&queue_len, d_queue_len, sizeof(unsigned int),
                            cudaMemcpyDeviceToHost));
    }
    if (fp64 || queue_len > queue_cap) {
      /* Todo en double: pedido explicitamente, o la cola se desbordo. */
      if (!fp64)
        LOG_WARN("Reprojection: %u borderline pixels overflow the queue (%zu); "
                 "redoing the whole grid in double.", queue_len, queue_cap);
      size_t n = (size_t)p.width * p.height;
      reproject_double_kernel<<<(unsigned int)((n + b1 - 1) / b1), b1>>>(
          d_src, d_out, p, d_nodata, nd, NULL, n);
    } else if (queue_len > 0) {
      reproject_double_kernel<<<(queue_len + b1 - 1) / b1, b1>>>(
          d_src, d_out, p, d_nodata, nd, d_queue, queue_len);
    }
    CUDA_CHECK(cudaGetLastError());
  }
  CUDA_CHECK(cudaEventRecord(t2));

  CUDA_CHECK(cudaMemcpy(geo_image.data, d_out, out_bytes, cudaMemcpyDeviceToHost));

  CUDA_CHECK(cudaEventRecord(t3));
  CUDA_CHECK(cudaEventSynchronize(t3));
  CUDA_CHECK(cudaEventElapsedTime(&ms_in, t0, t1));
  CUDA_CHECK(cudaEventElapsedTime(&ms_kernel, t1, t2));
  CUDA_CHECK(cudaEventElapsedTime(&ms_out, t2, t3));
  /* El kernel es la contraparte de la reproyeccion en CPU; las copias (y las
   * reservas que las acompanan) van a TM_XFER, como el resto de transferencias.
   * Hasta 2026-09-18 todo caia en TM_REPROJECT y la columna mezclaba PCIe con
   * aritmetica. LOG_TIMING concatena fmt con un literal: la variante va con %s. */
  LOG_TIMING_STAGE(TM_REPROJECT, ms_kernel / 1000.0, "Analytic reprojection (CUDA, %s)",
             fp64 ? "double" : "float, double al filo");
  if (!fp64)
    LOG_DEBUG("Reprojection: %u of %zu pixels redone in double (%.2f%%).", queue_len,
              (size_t)p.width * p.height,
              100.0 * queue_len / ((double)p.width * p.height));
  LOG_TIMING_STAGE(TM_XFER, (ms_in + ms_out) / 1000.0, "Reprojection transfers (%s)",
             d_src_owned ? "H2D fuente + D2H salida" : "fuente residente: solo D2H");
  ok = true;

cleanup:
  if (d_src_owned) cudaFree(d_src_owned);
  if (d_out) cudaFree(d_out);
  if (d_nodata) cudaFree(d_nodata);
  if (d_queue) cudaFree(d_queue);
  if (d_queue_len) cudaFree(d_queue_len);
  if (t0) cudaEventDestroy(t0);
  if (t1) cudaEventDestroy(t1);
  if (t2) cudaEventDestroy(t2);
  if (t3) cudaEventDestroy(t3);
  if (!ok) {
    free(geo_image.data);
    return image_create(0, 0, 0);
  }
  return geo_image;
}
