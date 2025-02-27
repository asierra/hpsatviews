/* NetCDF Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdlib.h>
#include <stdio.h>
#include <omp.h>

#include "datanc.h"


DataNC downsample_simple_nc(DataNC datanc_big, int factor)
{
  DataNC datanc;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  datanc.data_in = malloc(sizeof(float)*datanc.size);
  
  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datanc_big.height; j += factor)
    for (int i=0; i < datanc_big.width; i += factor) {
      int is = (j*datanc.width + i)/factor;
      datanc.data_in[is] = datanc_big.data_in[j*datanc_big.width + i];
    }
  double end = omp_get_wtime();

  return datanc;
}


DataNC downsample_boxfilter_nc(DataNC datanc_big, int factor)
{
  DataNC datanc;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  datanc.data_in = malloc(sizeof(float)*datanc.size);
  
  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datanc.height; j++) {
    int ny = factor;
    int jj = j*factor;
    if (jj+ny > datanc_big.height)
      ny -= datanc_big.height - (jj + ny);
    for (int i=0; i < datanc.width; i++) {
      int nx = factor;
      int ii = i*factor;
      if (ii+nx > datanc_big.width)
        nx -= datanc_big.width - (ii + nx);
      int acum = 0;
      double f = 0;
      for (int l=0; l < ny; l++) {
        int jx = (jj+l)*datanc_big.width;
        for (int k=0; k < nx; k++) {
          f += datanc_big.data_in[jx + ii + k];
          acum++;
        }
      }
      datanc.data_in[j*datanc.width + i] = (float)(f/acum);
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo downsampling boxfilter %lf\n", end - start);

  return datanc;
}