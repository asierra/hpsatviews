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

/* Umbrella público de la aceleración CUDA. Hoy toda la API vive en la capa
 * residente en device (cuda_dataf.h): subir el float una vez, encadenar
 * kernels (gamma -> gray -> ...) en GPU y bajar solo el uint8 resultante.
 * Ver el porqué (transferencia-bound) en docs/cuda-support/CUDA_PLAN.md. */
#include "cuda_dataf.h"
#include "cuda_truecolor.h"
#include "cuda_rayleigh.h"

#endif /* HPSATVIEWS_CUDA_KERNELS_H_ */
