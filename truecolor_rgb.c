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

ImageData create_truecolor_rgb(DataNC c01, DataNC c02, DataNC c03) {
  ImageData imout = image_create(c01.base.width, c01.base.height, 3);
  
  // Check if allocation was successful
  if (imout.data == NULL) {
    return imout; // Return empty image on allocation failure
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
      } 
      imout.data[po] = r;
      imout.data[po + 1] = g;
      imout.data[po + 2] = b;
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo RGB %lf\n", end - start);
  return imout;
}
