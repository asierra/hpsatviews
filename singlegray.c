/* Creates a single BW image from an original NC.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "image.h"
#include "logger.h"
#include "reader_cpt.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha, const CPTData* cpt) {
  unsigned int bpp = (use_alpha && cpt==NULL) ? 2 : 1;

  ImageData imout = image_create(c01.width, c01.height, bpp);
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen en singlegray.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando loop singlegray iw %lu ih %lu min %g max %g", imout.width, imout.height, c01.fmin, c01.fmax);

  uint8_t last_color = (cpt && cpt->has_nan_color) ? cpt->num_colors-1: 255;
  #pragma omp parallel for
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      uint8_t r = 0, a = 0;

      if (c01.type == DATA_TYPE_FLOAT) {
        float *data_in_f = (float*)c01.data_in;
        if (data_in_f[i] != NonData) {
          float dd = c01.fmax - c01.fmin;
          float f;
          if (invert_value)
            f = (c01.fmax - data_in_f[i]) / dd;
          else
            f = (data_in_f[i] - c01.fmin) / dd;
          r = (unsigned char)(last_color * f);
          a = 255;
        } else if (cpt && cpt->has_nan_color)  {
          // último color de la paleta es para NaN
          r = last_color;
        }
      } else { // Asumimos DATA_TYPE_INT8
        int8_t *data_in_b = (int8_t*)c01.data_in;
        // Para bytes con signo, el valor -128 es nuestro NonData
        if (data_in_b[i] != -128) {
          r = (uint8_t)data_in_b[i];
          a = 255;
        } else if (cpt && cpt->has_nan_color)  {
          // último color de la paleta es para NaN
          r = last_color;
        }
      }

      if (cpt != NULL && 0) {
        // Si hay una paleta, 'r' se convierte en un índice para esa paleta.
        // La lógica de escritura de PNG se encargará del resto.
        Color c = get_color_for_value(cpt, (double)r);
        // Para una imagen de un solo canal con paleta, el valor del píxel es el índice.
        // Aquí, simplemente usamos el valor de gris como un índice aproximado.
        // Una implementación más precisa mapearía el valor original (float o byte) al rango del CPT.
        // Por ahora, esto funciona para CPTs normalizados.
        r = (unsigned char)(((double)r / 255.0) * (cpt->entry_count > 0 ? cpt->entry_count - 1 : 255));
      }

      imout.data[po] = r;
      if (imout.bpp == 2) {
        a = 255;
        imout.data[po + 1] = a;
      }
    }
  }
  double end = omp_get_wtime();
  LOG_INFO("Tiempo Single Gray %lf", end - start);
  return imout;
}
