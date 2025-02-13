#ifndef _DATANC_H_
#define _DATANC_H_

#include <stdlib.h>


// Data structure to store data read from a NetCDF file
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

// Downsampling using Nearest-neighbor interpolation with integer factor
DataNC downsample_neighbor_nc(DataNC datanc_big, int factor);

#endif
