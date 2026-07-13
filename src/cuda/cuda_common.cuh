/* Shared device-side helpers for the CUDA kernels.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Incluido por los .cu de src/cuda/. No es API pública.
 */
#ifndef HPSV_CUDA_COMMON_CUH_
#define HPSV_CUDA_COMMON_CUH_

#include <cuda_runtime.h>
#include <math.h>

/* Equivalente device de IS_NONDATA (macro de host en datanc.h, que usa
 * isnan/isinf de <math.h>). NonData (1.0e+32, src/datanc.c) satisface el
 * umbral >= 1e30, así que este único chequeo cubre fill-value, NaN e Inf. */
__device__ __forceinline__ bool is_nondata_dev(float x) {
  return (x >= 1.0e+30f) || isnan(x) || isinf(x);
}

#endif /* HPSV_CUDA_COMMON_CUH_ */
