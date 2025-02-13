#ifndef _DATANC_H_
#define _DATANC_H_

#include <stdlib.h>


// En esta estructura se guardanlos datos NetCDF recuperados de disco
typedef struct {
  size_t width, height;
  size_t size;
  float *data_in;
  int year, mon, day, hour, min, sec;
  unsigned char band_id;
} DataNC;

typedef struct {
  size_t width, height;
  size_t size;
  float *data_in;
} DataNCF;

// Remuestreo seleccionando vecinos cercanos con factor entero
DataNC downsample_neighbor_nc(DataNC datanc_big, int factor);

#endif
