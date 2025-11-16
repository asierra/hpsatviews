/* Night composite image generation

   Esta versión es sencilla, sin fondo de luces de ciudad ni transparencia.

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
#include "paleta.h"
#include "logger.h"

ImageData create_nocturnal_pseudocolor(DataNC datanc) {
  // Asegurarnos de que estamos trabajando con datos float.
  if (!datanc.is_float) {
    LOG_ERROR("create_nocturnal_pseudocolor requiere datos de punto flotante (float).");
    return image_create(0, 0, 0);
  }

  ImageData imout = image_create(datanc.fdata.width, datanc.fdata.height, 3);
  
  // Check if allocation was successful
  if (imout.data == NULL) {
    return imout; // Return empty image on allocation failure
  }

  float tmin = 1e10, tmax = -1e10;

  double start = omp_get_wtime();
  float *temp_data = datanc.fdata.data_in;

#pragma omp parallel for shared(temp_data, imout) reduction(min:tmin) reduction(max:tmax)
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      unsigned char r, g, b;

      r = g = b = 0;
      if (temp_data[i] != NonData) {
        float f = temp_data[i];
        if (f < tmin)
          tmin = f;
        if (f > tmax)
          tmax = f;
        int t;
        for (t = 0; t < 255; t++)
          if (f >= paleta[t].d && f < paleta[t + 1].d)
            break;
        r = (unsigned char)(255 * paleta[t].r);
        g = (unsigned char)(255 * paleta[t].g);
        b = (unsigned char)(255 * paleta[t].b);

        imout.data[po] = r;
        imout.data[po + 1] = g;
        imout.data[po + 2] = b;
      }
    }
  }
  printf("temp min %g K   temp max %g K\n", tmin, tmax);
  double end = omp_get_wtime();
  printf("Tiempo pseudo %lf\n", end - start);

  return imout;
}
