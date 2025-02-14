/* NetCDF Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdlib.h>
#include <stdio.h>
#include <omp.h>

#include "datanc.h"


// Downsampling using Nearest-neighbor interpolation (to do) with integer factor
// Right now it just selects without interpolation.
DataNC downsample_neighbor_nc(DataNC datanc_big, int factor)
{
  DataNC datanc;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  printf("data size %d %d %d\n", datanc.width, datanc.height, datanc.size); fflush(stdout);
  datanc.data_in = malloc(sizeof(float)*datanc.size);
  
  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datanc_big.height; j += factor)
    for (int i=0; i < datanc_big.width; i += factor) {
      int is = (j*datanc.width + i)/factor;
      datanc.data_in[is] = datanc_big.data_in[j*datanc_big.width + i];
    }
  double end = omp_get_wtime();
  printf("Tiempo downsampling %lf\n", end - start);

  return datanc;
}
