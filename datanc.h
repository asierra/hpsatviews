/* Float Array Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef _DATANC_H_
#define _DATANC_H_

#include <stdlib.h>


typedef struct {
  size_t width, height;
  size_t size;
  float *data_in;
} DataF;

// Data structure to store data read from a NetCDF file
typedef struct {
  DataF base;
  int year, mon, day, hour, min, sec;
  unsigned char band_id;
} DataNC;


// Simple downsampling selecting points.
DataF downsample_simple(DataF datanc_big, int factor);

// Downsampling using Box Filter algorithm.
DataF downsample_boxfilter(DataF datanc_big, int factor);

#endif
