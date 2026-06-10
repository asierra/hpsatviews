/* Fixed-grid to geographic (lat/lon) reprojection for GOES-R ABI imagery.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_REPROJECTION_H_
#define HPSATVIEWS_REPROJECTION_H_

#include "datanc.h"
#include "image.h"

// Finds the nearest-neighbor pixel for a geographic coordinate in a navigation grid.
// navla/navlo: latitude and longitude grids. out_ix/out_iy receive column and row.
void reprojection_find_pixel_for_coord(const DataF* navla, const DataF* navlo,
                                       float target_lat, float target_lon,
                                       int* out_ix, int* out_iy);

// Computes the pixel bounding box covering a geographic domain by dense edge sampling.
// Returns the number of valid samples (0 if the domain is outside the Earth disk).
int reprojection_find_bounding_box(const DataF* navla, const DataF* navlo,
                                   float clip_lon_min, float clip_lat_max,
                                   float clip_lon_max, float clip_lat_min,
                                   int* out_x_start, int* out_y_start,
                                   int* out_width, int* out_height);

// Reprojects an image from GOES-R fixed-grid to geographic (lat/lon) projection
// using the analytical inverse scan-angle equations from GOES-R PUG Vol. 4.
// Bilinear interpolation; no gap filling. clip_coords is optional [lon_min,lat_max,lon_max,lat_min].
// Returns a new ImageData; caller must free it.
ImageData reproject_image_analytical(const ImageData* src_image, const DataNC* data_nc,
                                     float lat_min, float lat_max,
                                     float lon_min, float lon_max,
                                     float native_resolution_km,
                                     const float* clip_coords);

#endif /* HPSATVIEWS_REPROJECTION_H_ */