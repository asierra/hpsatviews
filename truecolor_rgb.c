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

ImageData create_truecolor_rgb(DataNC c01, DataNC c02, DataNC c03,
                                     unsigned char apply_histogram) {
  ImageData imout;
  imout.bpp = 3;
  imout.width = c01.base.width;
  imout.height = c01.base.height;
  imout.data = malloc(imout.bpp * c01.base.size);

  // Inicializamos histograma
  unsigned int histogram[255];
  if (apply_histogram) {
    for (int i = 0; i < 255; i++)
      histogram[i] = 0;
  }

  double start = omp_get_wtime();

#pragma omp parallel for shared(c01, c02, c03, imout)
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      unsigned char r, g, b;

      r = g = b = 0;
      if (c01.base.data_in[i] != NonData) {
        // Verde sintético con la combinación lineal de las 3 bandas
        float c01f = c01.base.data_in[i];
        float c02f = c02.base.data_in[i];
        float c03f = c03.base.data_in[i];

        float gg = 0.48358168 * c02f + 0.45706946 * c01f + 0.08038137 * c03f;
        // Generación de color visible para imagen
        r = (unsigned char)(255.0 * c02f);
        g = (unsigned char)(255.0 * gg);
        b = (unsigned char)(255.0 * c01f);

        // Luminosidad promedio
        unsigned int q = (unsigned int)((r + b + g + 0.5) / 3.0);
        histogram[q]++;
      } 
      imout.data[po] = r;
      imout.data[po + 1] = g;
      imout.data[po + 2] = b;
    }
  }

  // Igualación de histograma
  if (apply_histogram) {
    int cum = 0;
    unsigned char transfer[255]; // Función de transferencia
    for (int i = 0; i < 256; i++) {
      cum += histogram[i];
      transfer[i] = (unsigned char)(255.0 * cum / c01.base.size);
    }
#pragma omp parallel for shared(c01, imout.data)
    for (int i = 0; i < c01.base.size; i++) {
      int p = i * imout.bpp;
      if (c01.base.data_in[i] != NonData) {
        imout.data[p] = transfer[imout.data[p]];
        imout.data[p + 1] = transfer[imout.data[p + 1]];
        imout.data[p + 2] = transfer[imout.data[p + 2]];
      }
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo RGB %lf\n", end - start);
  return imout;
}
