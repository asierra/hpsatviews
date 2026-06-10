/* Nighttime pseudocolor imagery from ABI C13 brightness temperature.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <math.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>

#include "datanc.h"
#include "image.h"
#include "palette.h"
#include "logger.h"

ImageData create_nocturnal_pseudocolor(const DataF* temp_data, const ImageData* fondo) {
  if (!temp_data || !temp_data->data_in) {
    LOG_ERROR("Datos de temperatura inválidos para create_nocturnal_pseudocolor.");
    return image_create(0, 0, 0); // return empty image on invalid input
  }

  ImageData imout = image_create(temp_data->width, temp_data->height, 3);
  
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen nocturna.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando generación de pseudocolor nocturno...");

  const float max_ir_temp = 263.15f; // upper bound for high cold clouds (~-10°C)

#pragma omp parallel for
  for (unsigned int y = 0; y < imout.height; y++) {
    for (unsigned int x = 0; x < imout.width; x++) {
      size_t i = (size_t)y * imout.width + x;
      size_t po = i * imout.bpp;
      unsigned char r, g, b;

      r = g = b = 0;
      float f = temp_data->data_in[i];

      if (!IS_NONDATA(f)) {
        unsigned int t;
        for (t = 0; t < 255; t++)
          if (f >= atmosrainbow[t].d && f < atmosrainbow[t + 1].d)
            break;

        // If t == 255, the value is >= the last threshold; clamp to 254
        // to avoid an out-of-bounds access on paleta[t+1].
        if (t == 255) t = 254;

        r = (unsigned char)(255 * atmosrainbow[t].r);
        g = (unsigned char)(255 * atmosrainbow[t].g);
        b = (unsigned char)(255 * atmosrainbow[t].b);

        if (fondo && f > max_ir_temp) {
          float w = 1. - atmosrainbow[t].a;
          size_t pf = i * fondo->bpp;
          r = (unsigned char)(r * (1 - w) + w * fondo->data[pf]);
          g = (unsigned char)(g * (1 - w) + w * fondo->data[pf + 1]);
          b = (unsigned char)(b * (1 - w) + w * fondo->data[pf + 2]);
        }

        imout.data[po] = r;
        imout.data[po + 1] = g;
        imout.data[po + 2] = b;
      }
    }
  }

  double end = omp_get_wtime();
  LOG_TIMING(end - start, "Pseudocolor nocturno");

  return imout;
}
