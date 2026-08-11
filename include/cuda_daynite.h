/* Piezas del composite día/noche residentes en device: pseudocolor nocturno,
 * máscara día/noche y mezcla de ambas imágenes.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Completan lo que faltaba para que 'daynite' corra entero en GPU: el lado
 * diurno ya lo cubrían los kernels de truecolor (Rayleigh, verde, stretch,
 * compose). Trabajan sobre buffers de imagen de device (uint8, 3 bpp) para que
 * la cadena no toque host hasta la bajada final.
 *
 * Solo bajo #ifdef HPSV_CUDA; implementación en src/cuda/daynite_cuda.cu.
 */
#ifndef HPSATVIEWS_CUDA_DAYNITE_H_
#define HPSATVIEWS_CUDA_DAYNITE_H_

#include "cuda_dataf.h"
#include "daynight_mask.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pseudocolor nocturno a partir de la temperatura de brillo (C13) ya en device.
 * Réplica de create_nocturnal_pseudocolor() (src/nocturnal_pseudocolor.c),
 * incluida la paleta atmosrainbow y el compuesto opcional sobre luces de ciudad.
 *
 * d_fondo: imagen de luces ya en device, o NULL para desactivarlas.
 * d_out: recibe un buffer de device (uint8, 3 bpp, width*height*3 bytes) que
 * pasa a ser del llamador; liberar con cuda_free_device_image(). */
bool create_nocturnal_pseudocolor_dev(const DataFDev *temp,
                                      const unsigned char *d_fondo,
                                      unsigned int fondo_bpp,
                                      unsigned char **d_out);

/* Máscara día/noche (1 bpp: 255 = noche, 0 = día, intermedio = penumbra).
 * Réplica de create_daynight_mask() (src/daynight_mask.c). La efeméride se
 * calcula en host con solar_ephemeris_precompute() y llega como escalares, igual
 * que hace la ruta CPU, para que ambas partan de los mismos números.
 * dnratio recibe el porcentaje de píxeles diurnos, como la versión CPU. */
bool create_daynight_mask_dev(const DataFDev *temp, const DataFDev *navla,
                              const DataFDev *navlo, const SolarEphemeris_dn *eph,
                              float max_temp, unsigned char **d_mask,
                              float *dnratio);

/* Mezcla out = mask*bg + (1-mask)*fg sobre imágenes de 3 bpp ya en device.
 * Réplica de blend_images() (src/image.c). d_out recibe un buffer nuevo. */
bool blend_images_dev(const unsigned char *d_bg, const unsigned char *d_fg,
                      const unsigned char *d_mask, unsigned int width,
                      unsigned int height, unsigned char **d_out);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_DAYNITE_H_ */
