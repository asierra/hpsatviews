/* Main Floating Point Array Data Structure and tools
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_DATANC_H_
#define HPSATVIEWS_DATANC_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

extern float NonData;

// Fill/missing value check: matches NetCDF _FillValue convention (>= 1e30, NaN, Inf)
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

typedef enum {
    SECTOR_UNKNOWN = 0,
    SECTOR_FD,    // Full Disk
    SECTOR_CONUS, // CONUS
    SECTOR_M1,    // Mesoscale 1
    SECTOR_M2,    // Mesoscale 2
} SectorID;

typedef enum {
    LEVEL_UNKNOWN = 0,
    LEVEL_L1b,
    LEVEL_L2
} Level;

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
  SectorID sector_id;
  Level level;
  const char* varname;
  time_t timestamp;
  unsigned char band_id;
  float native_resolution_km; // Native ABI band resolution in km (0 if unknown)
  
  // [TopLeftX, PixelW, RotX, TopLeftY, RotY, PixelH]
  double geotransform[6]; 
  ProjectionCode proj_code;
  
  // GEOS projection params for WKT construction
  struct {
      double sat_height; // perspective_point_height
      double semi_major; // semi_major_axis
      double semi_minor; // semi_minor_axis
      double lon_origin; // longitude_of_projection_origin
      double inv_flat;   // inverse_flattening
      bool valid;        // true if projection params were read successfully
  } proj_info;
} DataNC;

/**
 * @brief Allocates a 2D float grid.
 * @return Initialized DataF; data_in is NULL on allocation failure.
 */
DataF dataf_create(unsigned int width, unsigned int height);

// Frees DataF memory. Safe to call with NULL data_in.
void dataf_destroy(DataF *data);

// Returns a deep copy of a DataF grid.
DataF dataf_copy(const DataF *data);

// Fills the DataF buffer with a constant value.
void dataf_fill(DataF *data, float value);

/**
 * @brief Extracts a rectangular subgrid from a DataF.
 * @param x_start, y_start  Top-left pixel of the crop window.
 * @param width, height      Crop dimensions in pixels.
 * @return New DataF containing the cropped region.
 */
DataF dataf_crop(const DataF *data, unsigned int x_start, unsigned int y_start, 
                 unsigned int width, unsigned int height);

// Nearest-neighbor decimation by integer factor.
DataF downsample_simple(DataF datanc_big, int factor);

// Box-filter (averaging) downsampling by integer factor.
DataF downsample_boxfilter(DataF datanc_big, int factor);

// Bilinear interpolation upsampling by integer factor.
DataF upsample_bilinear(DataF datanc_big, int factor);

// DataB
DataB datab_create(unsigned int width, unsigned int height);
void datab_destroy(DataB *data);

// DataF arithmetic operations

// Element-wise arithmetic between two DataF grids (ADD, SUB, MUL, DIV).
DataF dataf_op_dataf(const DataF* a, const DataF* b, Operation op);

// Element-wise arithmetic between a DataF grid and a scalar.
DataF dataf_op_scalar(const DataF* a, float scalar, Operation op, bool scalar_first);

// Negates all values in a DataF grid in-place.
void dataf_invert(DataF* a);

// DataNC utilities
void datanc_destroy(DataNC *datanc);
DataF datanc_get_float_base(DataNC *datanc);

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923

/**
 * @brief Applies gamma correction to float radiance data.
 * Normalizes to [min_val, max_val] then computes pixel = ((pixel-min)/(max-min))^(1/gamma).
 * Output values are in [0, 1].
 * @param gamma  Gamma exponent (e.g., 2.0 for square-root stretch).
 * @param min_val, max_val  Radiance range for linear stretch.
 */
void dataf_apply_gamma(DataF *data, float gamma, float min_val, float max_val);

/**
 * @brief 2×2 block-average filter (same output size as input).
 * Handles odd dimensions and skips fill values.
 * @return New DataF with averaged values.
 */
DataF dataf_mean_2x2(const DataF *input);

#endif /* HPSATVIEWS_DATANC_H_ */
