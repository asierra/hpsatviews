/* NetCDF Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
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


// Simple downsampling selecting points.
DataNC downsample_simple_nc(DataNC datanc_big, int factor);

// Downsampling using Box Filter algorithm.
DataNC downsample_boxfilter_nc(DataNC datanc_big, int factor);

#endif
