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

#endif /* HPSATVIEWS_PROJECTION_H_ */
