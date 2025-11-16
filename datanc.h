/* Floating point Array Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_DATANC_H_
#define HPSATVIEWS_DATANC_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

extern float NonData;

// Enum for arithmetic operations
typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV
} Operation;

// A 2D grid structure for floating-point data.
typedef struct {
  size_t width, height;
  size_t size;
  float *data_in;
  float fmin, fmax;
} DataF;

// A 2D grid structure for 8-bit signed integer data.
typedef struct {
  size_t width, height;
  size_t size;
  int8_t *data_in;
  int8_t min, max;
} DataB;

// Data structure to store metadata read from a NetCDF file
typedef struct {
  DataF fdata; // Used if the data is float
  DataB bdata; // Used if the data is byte
  bool is_float; // True if fdata is valid, false if bdata is valid
  int year, mon, day, hour, min, sec;
  unsigned char band_id;
} DataNC;


//inline float dataf_value(DataF data, unsigned i, unsigned j) {
 // unsigned ii = j * data.width + i;
 // return data.data_in[ii];
//}

// Constructor: creates a new DataF structure with allocated memory
// Returns initialized DataF on success, or DataF with NULL data_in on failure
DataF dataf_create(size_t width, size_t height);

// Destructor: safely frees memory allocated for DataF
// Safe to call with NULL data_in pointer
void dataf_destroy(DataF *data);

DataF dataf_copy(const DataF *data);

void dataf_fill(DataF *data, float value);

// Simple downsampling selecting points.
DataF downsample_simple(DataF datanc_big, int factor);

// Downsampling using Box Filter algorithm for float data.
DataF downsample_boxfilter(DataF datanc_big, int factor);

// Upsampling using bilinear algorithm for float data.
DataF upsample_bilinear(DataF datanc_big, int factor);

// --- Funciones para DataB ---
DataB datab_create(size_t width, size_t height);
void datab_destroy(DataB *data);

// --- Funciones de operaciones aritméticas para DataF ---
DataF dataf_op_dataf(const DataF* a, const DataF* b, Operation op);
DataF dataf_op_scalar(const DataF* a, float scalar, Operation op, bool scalar_first);
void dataf_invert(DataF* a);

// --- Funciones de utilidad para DataNC ---
void datanc_destroy(DataNC *datanc);
DataF datanc_get_float_base(DataNC *datanc);

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923

#endif /* HPSATVIEWS_DATANC_H_ */
