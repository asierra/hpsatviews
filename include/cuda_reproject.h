/* Device port of the fixed-grid -> geographic reprojection (a per-output-pixel
 * inverse scan-angle gather). Compute-heavy trig per pixel, so it benefits from
 * the GPU unlike the trivial per-pixel kernels.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Solo bajo #ifdef HPSV_CUDA; implementación en src/cuda/reproject_cuda.cu.
 */
#ifndef HPSATVIEWS_CUDA_REPROJECT_H_
#define HPSATVIEWS_CUDA_REPROJECT_H_

#include "image.h"
#include "datanc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Drop-in CUDA equivalent of reproject_image_analytical() (src/reprojection.c):
 * same output. Reuses reproject_build_plan() for the projection setup, runs one
 * thread per output pixel (nearest-neighbor for bpp==1, bilinear otherwise) and
 * downloads the result. Returns ImageData with data==NULL on failure.
 *
 * d_src_image: puntero de device con la imagen fuente ya subida, o NULL para
 * subirla desde src_image->data como antes. Sirve para encadenar con
 * create_multiband_rgb_from_dev(), que puede conservar su salida en device y
 * ahorrarse el H2D de aquí. Cuando no es NULL, src_image se sigue usando por sus
 * dimensiones/bpp, que deben coincidir con las del buffer de device. La
 * propiedad del buffer NO se transfiere: lo sigue liberando el llamador. */
ImageData reproject_image_analytical_cuda(const ImageData* src_image, const DataNC* data_nc,
                                          float lat_min, float lat_max,
                                          float lon_min, float lon_max,
                                          float native_resolution_km,
                                          const float* clip_coords,
                                          const unsigned char* nodata_pixel,
                                          const unsigned char* d_src_image);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_REPROJECT_H_ */
