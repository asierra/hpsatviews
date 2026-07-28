/* Selección y reporte del dispositivo CUDA.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Solo tiene sentido bajo #ifdef HPSV_CUDA; la implementación vive en
 * src/cuda/device_info.cu.
 */
#ifndef HPSATVIEWS_CUDA_DEVICE_H_
#define HPSATVIEWS_CUDA_DEVICE_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Comprueba que haya una GPU utilizable y reporta cuál (LOG_INFO: nombre,
 * compute capability, memoria libre/total). Pensada para llamarse una vez, al
 * resolver --cuda, de modo que un servidor compartido deje registro de qué
 * dispositivo agarró la corrida y el fallo sea temprano y explícito en vez de
 * un error genérico a media pipeline.
 *
 * Devuelve false (con LOG_ERROR) si no hay dispositivo, si el driver falla, o
 * si la compute capability del dispositivo no corresponde a la arquitectura
 * con la que se compiló el binario (CUDA_ARCH). Idempotente: llamadas
 * posteriores reusan el resultado de la primera sin volver a loguear. */
bool cuda_report_device(void);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_DEVICE_H_ */
