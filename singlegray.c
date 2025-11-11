/* Creates a single BW image from an original NC.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "image.h"
#include "logger.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha) {
  unsigned int bpp = (use_alpha) ? 2 : 1;
  ImageData imout = image_create(c01.width, c01.height, bpp);
  
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen en singlegray.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando loop singlegray iw %lu ih %lu min %g max %g", imout.width, imout.height, c01.fmin, c01.fmax);
  float dd = c01.fmax - c01.fmin;
  #pragma omp parallel for
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      unsigned char r = 0, a = 0;
      if (c01.data_in[i] != NonData) {
        float f;
        if (invert_value)
          f = (c01.fmax - c01.data_in[i]) / dd;
        else
          f = (c01.data_in[i] - c01.fmin) / dd;
        r = (unsigned char)(255.0 * f);
        a = 255;
      }
      imout.data[po] = r;
      if (imout.bpp == 2)
        imout.data[po + 1] = a;
    }
  }
  double end = omp_get_wtime();
  LOG_INFO("Tiempo Single Gray %lf", end - start);
  return imout;
}
