/* True color composite image generation
 * Copyright (c) 2024  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datanc.h"
#include "image.h"

ImageData create_truecolor_composite(DataNC c01, DataNC c02, DataNC c03) {
  ImageData imout;
  imout.bpp = 4;
  imout.width = c01.width;
  imout.height = c01.height;
  imout.data = malloc(imout.bpp * c01.size);

  double start = omp_get_wtime();

#pragma omp parallel for shared(c01, c02, c03, imout)
  for (int y = 0; y < c01.height; y++) {
    for (int x = 0; x < c01.width; x++) {
      int i = y * c01.width + x;
      int po = i * imout.bpp;
      unsigned char r, g, b, a;

      r = g = b = 0;
      a = 0;
      if (c01.data_in[i] >= 0 && c01.data_in[i] < 4095) {
        // Verde sintético con la combinación lineal de las 3 bandas
        float gg = 0.48358168 * c02.data_in[i] + 0.45706946 * c01.data_in[i] +
                   0.08038137 * c03.data_in[i];
        // Generación de color visible para imagen
        r = (unsigned char)(255.0 * c02.data_in[i] / 4095.0);
        g = (unsigned char)(255.0 * gg / 4095.0);
        b = (unsigned char)(255.0 * c01.data_in[i] / 4095.0);

        imout.data[po] = r;
        imout.data[po + 1] = g;
        imout.data[po + 2] = b;
        imout.data[po + 3] = a;
      }
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo pseudo %lf\n", end - start);

  return imout;
}
