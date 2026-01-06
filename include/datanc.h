/* Floating point Array Data structure and tools
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_DATANC_H_
#define HPSATVIEWS_DATANC_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

extern float NonData;

// Macro para verificar si un valor es NonData (comparación robusta para floats)
#define IS_NONDATA(x) ((x) >= 1.0e+30f || isnan(x) || isinf(x))

// Enum for arithmetic operations
typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV
} Operation;

// A 2D grid structure for floating-point data.
typedef struct {
  unsigned int width, height;
  size_t size;
  float *data_in;
  float fmin, fmax;
} DataF;

// A 2D grid structure for 8-bit signed integer data.
typedef struct {
  unsigned int width, height;
  size_t size;
  int8_t *data_in;
  int8_t min, max;
} DataB;

typedef enum {
    SAT_UNKNOWN = 0,
    SAT_GOES16,
    SAT_GOES17,
    SAT_GOES18,
    SAT_GOES19,
} SatelliteID;

static const char *SAT_NAMES[] = {
    [SAT_UNKNOWN] = "unknown",
    [SAT_GOES16]  = "G16",
    [SAT_GOES17]  = "G17",
    [SAT_GOES18]  = "G18",
    [SAT_GOES19]  = "G19"
};

static const char* get_sat_name(SatelliteID id) {
    if (id >= SAT_UNKNOWN && id <= SAT_GOES19) {
        return SAT_NAMES[id];
    }
    return "unknown";
}

typedef enum {
  PROJ_GEOS = 0,   // GOES-R ABI Fixed Grid
  PROJ_LATLON = 1, // Equirrectangular / Plate Carrée (EPSG:4326)
  PROJ_UNKNOWN = 255
} ProjectionCode;
  
// Data structure to store metadata read from a NetCDF file
typedef struct {
  DataF fdata; // Used if the data is float
  DataB bdata; // Used if the data is byte
  bool is_float; // True if fdata is valid, false if bdata is valid
  SatelliteID sat_id;
  const char* varname;
  time_t timestamp;
  unsigned char band_id;
  float native_resolution_km; // Resolución nativa del sensor en km (0 si desconocida)
  
  // [TopLeftX, PixelW, RotX, TopLeftY, RotY, PixelH]
  double geotransform[6]; 
  ProjectionCode proj_code;
  
  // Parámetros para construir el WKT (Well Known Text) 
  struct {
      double sat_height; // perspective_point_height
      double semi_major; // semi_major_axis
      double semi_minor; // semi_minor_axis
      double lon_origin; // longitude_of_projection_origin
      double inv_flat;   // inverse_flattening (opcional, calculable con a/b)
      bool valid;        // Flag para saber si se leyeron correctamente
  } proj_info;
} DataNC;

// Constructor: creates a new DataF structure with allocated memory
// Returns initialized DataF on success, or DataF with NULL data_in on failure
DataF dataf_create(unsigned int width, unsigned int height);

// Destructor: safely frees memory allocated for DataF
// Safe to call with NULL data_in pointer
void dataf_destroy(DataF *data);

DataF dataf_copy(const DataF *data);

void dataf_fill(DataF *data, float value);

// Crops a rectangular region from a DataF structure
DataF dataf_crop(const DataF *data, unsigned int x_start, unsigned int y_start, 
                 unsigned int width, unsigned int height);

// Simple downsampling selecting points.
DataF downsample_simple(DataF datanc_big, int factor);

// Downsampling using Box Filter algorithm for float data.
DataF downsample_boxfilter(DataF datanc_big, int factor);

// Upsampling using bilinear algorithm for float data.
DataF upsample_bilinear(DataF datanc_big, int factor);

// --- Funciones para DataB ---
DataB datab_create(unsigned int width, unsigned int height);
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

/**
 * @brief Aplica corrección gamma a nivel de datos flotantes.
 * Formula: pixel = pixel^(1/gamma)
 * @param data Puntero a la estructura DataF.
 * @param gamma Valor de gamma (ej. 2.0 para raíz cuadrada).
 */
void dataf_apply_gamma(DataF *data, float gamma);

#endif /* HPSATVIEWS_DATANC_H_ */
