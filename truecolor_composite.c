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
  imout.bpp = 3;
  imout.width = c01.width;
  imout.height = c01.height;
  imout.data = malloc(imout.bpp * c01.size);

  double start = omp_get_wtime();
  float min = 10000, max = -min;
  int count = 0;
#pragma omp parallel for shared(c01, c02, c03, imout)
  for (int y = 0; y < c01.height; y++) {
    for (int x = 0; x < c01.width; x++) {
      int i = y * c01.width + x;
      int po = i * imout.bpp;
      unsigned char r, g, b;

      r = g = b = 0;
      if (c01.data_in[i] >= 0 && c01.data_in[i] < 4095) {
        // Verde sintético con la combinación lineal de las 3 bandas
        float c01f = c01.scale_factor * c01.data_in[i] + c01.add_offset;
        float c02f = c02.scale_factor * c02.data_in[i] + c02.add_offset;
        float c03f = c03.scale_factor * c03.data_in[i] + c03.add_offset;

        float gg = 0.48358168 * c02f + 0.45706946 * c01f +
                   0.08038137 * c03f;
        // Generación de color visible para imagen
        r = (unsigned char)(255.0 * c02f);
        g = (unsigned char)(255.0 * gg);
        b = (unsigned char)(255.0 * c01f);

        imout.data[po] = r;
        imout.data[po + 1] = g;
        imout.data[po + 2] = b;
        count++;
        if (c01f < min)
          min = c01f;
        if (c01f > max)
          max = c01f;
      } else
        printf("puto\n");
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo composite %lf\n", end - start);
  printf("minmax %g %g %d\n", min, max, count);
  return imout;
}
