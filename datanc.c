/* NetCDF Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include <math.h>

#include "datanc.h"

float NonData=1.0e+32;


DataF downsample_simple(DataF datanc_big, int factor)
{
  DataF datanc;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  datanc.fmin = datanc_big.fmin;
  datanc.fmax = datanc_big.fmax;
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


DataF downsample_boxfilter(DataF datanc_big, int factor)
{
  DataF datanc;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  datanc.fmin = datanc_big.fmin;
  datanc.fmax = datanc_big.fmax;
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


DataF upsample_bilinear(DataF datanc_small, int factor)
{
  DataF datanc;
  datanc.width  = datanc_small.width*factor;
  datanc.height = datanc_small.height*factor;
  datanc.size = datanc.width * datanc.height;
  datanc.fmin = datanc_small.fmin;
  datanc.fmax = datanc_small.fmax;
  datanc.data_in = malloc(sizeof(float)*datanc.size);
  float xrat = (float)(datanc_small.width - 1)/(datanc.width - 1);
  float yrat = (float)(datanc_small.height - 1)/(datanc.height - 1);

  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_small, factor) 
  for (int j=0; j < datanc.height; j++) {
    for (int i=0; i < datanc.width; i++) {
      float x = xrat * i;
      float y = yrat * j;
      int xl = (int)floor(x);
      int yl = (int)floor(y);
      int xh = (int)ceil(x);
      int yh = (int)ceil(y);
      float xw = x - xl;
      float yw = y - yl;

      double d = datanc_small.data_in[yl*datanc_small.width + xl]*(1 -xw)*(1 - yw) +
        datanc_small.data_in[yl*datanc_small.width + xh]*xw*(1 - yw) + 
        datanc_small.data_in[yh*datanc_small.width + xl]*(1 -xw)*yw +
        datanc_small.data_in[yh*datanc_small.width + xh]*xw*yw;
      datanc.data_in[j*datanc.width + i] = (float)d;
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo upsampling bilinear %lf\n", end - start);

  return datanc;
}

