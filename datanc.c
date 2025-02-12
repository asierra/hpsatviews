#include <stdlib.h>
#include <stdio.h>
#include <omp.h>

#include "datanc.h"


// Remuestreo seleccionando vecinos cercanos con factor entero
DataNC downsample_neighbor_nc(DataNC datanc_big, int factor)
{
  DataNC datanc;
  datanc.add_offset   = datanc_big.add_offset;
  datanc.scale_factor = datanc_big.scale_factor;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  datanc.data_in = malloc(sizeof(short)*datanc.size);
  
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
