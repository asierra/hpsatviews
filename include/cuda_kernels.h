/* CUDA-accelerated view generation kernels.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Firmas idénticas a sus contrapartes en gray.h/rgb.h/etc. para que
 * puedan usarse como reemplazo drop-in seleccionable en runtime (--cuda).
 * Solo tiene sentido incluir este header bajo #ifdef HPSV_CUDA: las
 * implementaciones viven en src/cuda/*.cu y solo se compilan con CUDA=1.
 */
#ifndef HPSATVIEWS_CUDA_KERNELS_H_
#define HPSATVIEWS_CUDA_KERNELS_H_

#include "image.h"
#include "datanc.h"
#include "reader_cpt.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Equivalente CUDA de create_single_gray() (src/gray.c). Devuelve una
 * ImageData con data == NULL si falla cualquier operación CUDA. */
ImageData create_single_gray_cuda(DataF c01, bool invert_value, bool use_alpha,
                                  float min_val, float max_val,
                                  const CPTData* cpt);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_KERNELS_H_ */
