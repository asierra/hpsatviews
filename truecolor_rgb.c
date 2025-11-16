/* True color RGB image generation
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datanc.h"
#include "image.h"
#include "logger.h"
#include "rgb.h"

ImageData create_truecolor_rgb(DataF c01_blue, DataF c02_red, DataF c03_nir) {
  double start = omp_get_wtime();

  // Crear el canal verde sintético
  DataF green_ch = dataf_create(c02_red.width, c02_red.height);
  if (green_ch.data_in == NULL) {
    LOG_ERROR("Fallo de memoria al crear el canal verde sintético.");
    return image_create(0, 0, 0);
  }

  #pragma omp parallel for
  for (size_t i = 0; i < green_ch.size; i++) {
      float r = c02_red.data_in[i];
      float b = c01_blue.data_in[i];
      float n = c03_nir.data_in[i];
      if (r == NonData || b == NonData || n == NonData) {
          green_ch.data_in[i] = NonData;
      } else {
          green_ch.data_in[i] = 0.48358168f * r + 0.45706946f * b + 0.06038137f * n;
      }
  }

  // Usar la función genérica con los rangos estándar para color verdadero (0-1)
  ImageData imout = create_multiband_rgb(&c02_red, &green_ch, &c01_blue, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
  dataf_destroy(&green_ch);

  double end = omp_get_wtime();
  LOG_INFO("Tiempo RGB %lf\n", end - start);
  return imout;
}
