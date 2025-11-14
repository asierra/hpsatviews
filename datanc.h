/* Floating point Array Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_DATANC_H_
#define HPSATVIEWS_DATANC_H_

#include <stdlib.h>

extern float NonData;

typedef enum {
    DATA_TYPE_FLOAT,
    DATA_TYPE_INT8
} DataType;

// Like a numpy array to store 2D gridded floating point data, or sometimes, byte data
typedef struct {
  size_t width, height;
  size_t size;
  void *data_in;
  DataType type;
  float fmin, fmax;
} DataF;

// Data structure to store metadata read from a NetCDF file
typedef struct {
  DataF base;
  int year, mon, day, hour, min, sec;
  unsigned char band_id;
} DataNC;


//inline float dataf_value(DataF data, unsigned i, unsigned j) {
 // unsigned ii = j*data.width + i;
 // return data.data_in[ii];
//}

// Constructor: creates a new DataF structure with allocated memory
// Returns initialized DataF on success, or DataF with NULL data_in on failure
DataF dataf_create(size_t width, size_t height, DataType t);

// Destructor: safely frees memory allocated for DataF
// Safe to call with NULL data_in pointer
void dataf_destroy(DataF *data);

DataF dataf_copy(const DataF *data);

void dataf_fill(DataF *data, float value);

// Simple downsampling selecting points.
DataF downsample_simple(DataF datanc_big, int factor);

// Downsampling using Box Filter algorithm.
DataF downsample_boxfilter(DataF datanc_big, int factor);

// Upsampling using bilinear algorithm.
DataF upsample_bilinear(DataF datanc_big, int factor);

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923

#endif /* HPSATVIEWS_DATANC_H_ */
