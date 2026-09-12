/* Shared construction of the output spatial reference system.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * The GeoTIFF writer and the geographic footprint must describe the same CRS;
 * they used to build it separately, so this is the single place that turns a
 * DataNC's projection attributes into PROJ.4, WKT or an OGR handle.
 */
#ifndef HPSATVIEWS_PROJECTION_H_
#define HPSATVIEWS_PROJECTION_H_

#include <ogr_srs_api.h>
#include <stddef.h>
#include "datanc.h"

/// Writes the PROJ.4 string for the file's projection into buf.
/// Returns buf, or NULL if the projection is unknown or its params are missing.
const char* projection_proj4_from_nc(const DataNC *meta, char *buf, size_t size);

/// Builds the OGR spatial reference. Caller frees with OSRDestroySpatialReference().
/// Returns NULL if the projection is unknown.
OGRSpatialReferenceH projection_srs_from_nc(const DataNC *meta);

/// WKT of the file's projection. Caller frees with CPLFree(). NULL on failure.
char* projection_wkt_from_nc(const DataNC *meta);

/// WKT2 (2019), which is what STAC's proj:wkt2 asks for; the plain WKT above is
/// WKT1. Caller frees with CPLFree(). NULL on failure.
char* projection_wkt2_from_nc(const DataNC *meta);

/// Geotransform of the OUTPUT raster, in the same units as src_gt: the source
/// transform moved to the crop origin and stretched by the resampling factor
/// (negative scale reduces, positive enlarges, 1 leaves it alone). Every writer
/// used to inline this, but only inside its GeoTIFF branch, so a PNG had no
/// transform at all and nothing could describe its grid.
void projection_output_geotransform(const double src_gt[6], unsigned crop_x,
                                    unsigned crop_y, int scale, double out_gt[6]);

/// The output transform in STAC's proj:transform order and in the CRS's own
/// units: pixel width, row rotation, origin x, column rotation, pixel height,
/// origin y. NOT GDAL's order, and for the fixed grid the file's scan-angle
/// radians are scaled to metres, the same way the GeoTIFF writer does.
void projection_stac_transform(const DataNC *meta, unsigned crop_x, unsigned crop_y,
                               int scale, double stac[6]);

/// Frees a WKT returned by this module. Exists so callers need not include
/// GDAL headers just to release the string.
void projection_free_wkt(char *wkt);

#endif /* HPSATVIEWS_PROJECTION_H_ */
