/* Device-resident DataF: keeps a float grid in GPU memory so a chain of
 * kernels (upload -> gamma -> gray -> ... -> download) pays the H2D transfer
 * once instead of once per operation.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Motivación (benchmark 2026-07-13, RTX 5060 Ti vs OpenMP 6 cores): un kernel
 * trivial por-píxel (gray) es ~2x más lento que OpenMP porque la transferencia
 * H2D/D2H domina y el cómputo es despreciable. La GPU solo gana cuando la
 * transferencia se amortiza sobre varias operaciones encadenadas en device.
 * Esta capa es el prerrequisito para eso.
 *
 * Solo tiene sentido bajo #ifdef HPSV_CUDA; la implementación vive en
 * src/cuda/dataf_dev.cu (lifecycle + gamma) y src/cuda/gray_cuda.cu (gray).
 */
#ifndef HPSATVIEWS_CUDA_DATAF_H_
#define HPSATVIEWS_CUDA_DATAF_H_

#include "datanc.h"
#include "image.h"
#include "reader_cpt.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Un DataF cuyo buffer de píxeles vive en memoria de GPU. Refleja la geometría
 * de DataF; d_data es un puntero de device (cudaMalloc), NO desreferenciable
 * desde host. fmin/fmax se mantienen en el lado host como metadato del rango. */
typedef struct {
  unsigned int width, height;
  size_t size;    /* número de píxeles (width*height) */
  float *d_data;  /* puntero de device, o NULL si falló la reserva */
  float fmin, fmax;
} DataFDev;

/* Sube un DataF de host a un buffer de device recién reservado (cudaMalloc +
 * H2D). En fallo devuelve un DataFDev con d_data == NULL. Loguea el tiempo de
 * la transferencia por separado (desglose para el análisis de rendimiento). */
DataFDev dataf_dev_upload(const DataF *host);

/* Reserva un buffer de device de width*height floats sin subir nada (para
 * resultados computados en device, p.ej. verde sintético o ángulos de
 * navegación). d_data == NULL si falla. */
DataFDev dataf_dev_alloc(unsigned int width, unsigned int height);

/* Libera el buffer de device y deja el struct en cero. Seguro con d_data==NULL. */
void dataf_dev_destroy(DataFDev *dev);

/* Baja un DataFDev a un DataF de host recién reservado (el llamador lo libera
 * con dataf_destroy). Sirve para reinsertar en la cadena CPU un resultado
 * calculado en GPU, cuando el resto del pipeline aún no está portado. Devuelve
 * false ante fallo, dejando host_out en cero. */
bool dataf_dev_download(const DataFDev *dev, DataF *host_out);

/* Corrección gamma residente en device, in place. Misma semántica que
 * dataf_apply_gamma() (src/datanc.c): normaliza a [min,max], salida =
 * norm^(1/gamma), NonData intacto. Actualiza fmin/fmax a [0,1] cuando corre.
 * No-op para gamma==1 o rango degenerado (idéntico a la versión CPU). */
void dataf_dev_apply_gamma(DataFDev *dev, float gamma, float min_val,
                           float max_val);

/* Vista gris a partir de un DataF residente en device: corre el kernel gray
 * sobre d_data y copia de vuelta SOLO la imagen uint8 resultante (mucho menor
 * que el float de entrada). Salida idéntica a create_single_gray(). Devuelve
 * ImageData con data == NULL si falla cualquier operación CUDA. No libera el
 * DataFDev de entrada (lo posee el llamador). */
ImageData create_single_gray_from_dev(const DataFDev *dev, bool invert_value,
                                      bool use_alpha, float min_val,
                                      float max_val, const CPTData *cpt);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_DATAF_H_ */
