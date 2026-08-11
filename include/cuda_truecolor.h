/* Device-resident true-color composition kernels (synthetic green + RGB
 * compose), operating on DataFDev buffers so the default true-color chain
 * (upload C01/C02/C03 -> green -> gamma -> compose -> download) stays on the
 * GPU, paying the H2D transfer once.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Solo bajo #ifdef HPSV_CUDA; implementación en src/cuda/truecolor_cuda.cu.
 */
#ifndef HPSATVIEWS_CUDA_TRUECOLOR_H_
#define HPSATVIEWS_CUDA_TRUECOLOR_H_

#include "cuda_dataf.h"
#include "image.h"
#include "truecolor.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Verde sintético residente en device: G = 0.465*B + 0.465*R + 0.07*NIR,
 * NonData si alguna entrada lo es. Réplica de create_truecolor_synthetic_green()
 * (src/truecolor.c) SIN la reducción min/max: en truecolor el rango de render
 * es fijo (0..1.1), así que green.fmin/fmax no se usan. Reserva un buffer de
 * device nuevo; devuelve DataFDev con d_data==NULL si falla. */
DataFDev create_truecolor_green_from_dev(const DataFDev *blue,
                                         const DataFDev *red,
                                         const DataFDev *nir);

/* Composición RGB de 3 DataFDev a imagen uint8 (3 bpp), con stretch lineal
 * por canal y clamp a [0,1]. Réplica de create_multiband_rgb() (src/truecolor.c).
 * Baja la imagen resultante a host. ImageData con data==NULL si falla.
 *
 * d_retain: si no es NULL, además conserva el buffer de device con la imagen y
 * escribe ahí su puntero, para que la reproyección pueda consumirlo sin volver a
 * subirlo (ver reproject_image_analytical_cuda). El llamador pasa a ser dueño de
 * ese buffer y debe liberarlo con cuda_free_device_image(). Con NULL, el buffer
 * se libera aquí y el comportamiento es el de siempre. La copia D2H se hace en
 * ambos casos: hay código host (apply_enhancements) que consume la imagen. */
ImageData create_multiband_rgb_from_dev(const DataFDev *r, const DataFDev *g,
                                        const DataFDev *b, float r_min,
                                        float r_max, float g_min, float g_max,
                                        float b_min, float b_max,
                                        unsigned char **d_retain);

/* Stretch piecewise in place sobre un DataFDev. Réplica de
 * apply_piecewise_stretch() (src/truecolor.c) con la misma curva
 * GEO2GRID_STRETCH_X/Y. Deja fmin/fmax en [0,1]. false ante fallo CUDA. */
bool apply_piecewise_stretch_dev(DataFDev *band);

/* Copia a host un buffer de imagen de device. Mantiene la API de CUDA fuera del
 * código C del pipeline. false ante fallo. */
bool cuda_download_device_image(const unsigned char *d_image, unsigned char *host,
                                size_t bytes);

/* Libera un buffer de imagen de device obtenido vía d_retain. Seguro con NULL. */
void cuda_free_device_image(unsigned char *d_image);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_TRUECOLOR_H_ */
