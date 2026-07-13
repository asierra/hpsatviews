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

/// Finds the nearest-neighbor pixel for a geographic coordinate in a navigation grid.
void reprojection_find_pixel_for_coord(const DataF* navla, const DataF* navlo,
                                       float target_lat, float target_lon,
                                       int* out_ix, int* out_iy);

/// Computes the pixel bounding box covering a geographic domain by dense edge sampling.
int reprojection_find_bounding_box(const DataF* navla, const DataF* navlo,
                                   float clip_lon_min, float clip_lat_max,
                                   float clip_lon_max, float clip_lat_min,
                                   int* out_x_start, int* out_y_start,
                                   int* out_width, int* out_height);

/**
 * Reprojects an image from GOES-R fixed-grid to geographic (lat/lon) projection
 * using the analytical inverse scan-angle equations from GOES-R PUG Vol. 4.
 *
 * @param clip_coords Optional [lon_min, lat_max, lon_max, lat_min].
 * @param nodata_pixel Byte pattern of length src_image->bpp for out-of-bounds pixels. Pass NULL to fill with zero bytes.
 */
ImageData reproject_image_analytical(const ImageData* src_image, const DataNC* data_nc,
                                     float lat_min, float lat_max,
                                     float lon_min, float lon_max,
                                     float native_resolution_km,
                                     const float* clip_coords,
                                     const unsigned char* nodata_pixel);

/* Precomputed reprojection geometry: everything the per-output-pixel inverse
 * scan-angle math needs, computed once. Shared by the CPU loop and the CUDA
 * gather kernel so both use the exact same projection setup. */
typedef struct {
    unsigned int width, height, bpp;   ///< output dims + bytes/pixel (width==0 on failure)
    unsigned int src_w, src_h;         ///< source (fixed-grid) dims
    double target_lon_min, target_lat_max;
    double deg_per_px_lon, deg_per_px_lat;
    double b2_over_a2, e2, b, H, a2_over_b2, lambda0;
    double safe_gt[6];
} ReprojPlan;

/// Builds the reprojection plan (output size, target extent, projection params).
/// Returns a plan with width==0 if the inputs are invalid.
ReprojPlan reproject_build_plan(const ImageData* src_image, const DataNC* data_nc,
                                float lat_min, float lat_max, float lon_min, float lon_max,
                                float native_resolution_km, const float* clip_coords);

#endif /* HPSATVIEWS_REPROJECTION_H_ */