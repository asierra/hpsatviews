/* Shared construction of the output spatial reference system.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "projection.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

const char* projection_proj4_from_nc(const DataNC *meta, char *buf, size_t size) {
    if (meta == NULL || buf == NULL) return NULL;
    if (meta->proj_code != PROJ_GEOS || !meta->proj_info.valid) return NULL;

    // +sweep is CRUCIAL for GOES-R and it is not a constant of the projection:
    // it comes from the file's sweep_angle_axis (ABI writes "x", the Meteosat
    // family "y"). It used to be hardcoded here.
    const char *sweep = meta->proj_info.sweep[0] ? meta->proj_info.sweep : "x";
    snprintf(buf, size,
             "+proj=geos +sweep=%s +lon_0=%.6f +h=%.3f +x_0=0 +y_0=0 +ellps=GRS80 +units=m +no_defs",
             sweep, meta->proj_info.lon_origin, meta->proj_info.sat_height);
    return buf;
}

OGRSpatialReferenceH projection_srs_from_nc(const DataNC *meta) {
    if (meta == NULL) return NULL;
    OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);

    if (meta->proj_code == PROJ_GEOS && meta->proj_info.valid) {
        char proj4[512];
        projection_proj4_from_nc(meta, proj4, sizeof(proj4));
        if (OSRImportFromProj4(hSRS, proj4) != OGRERR_NONE) {
            LOG_ERROR("Error importing PROJ.4 projection: %s", proj4);
            OSRDestroySpatialReference(hSRS);
            return NULL;
        }
    } else if (meta->proj_code == PROJ_LATLON) {
        OSRImportFromEPSG(hSRS, 4326);
        OSRSetAxisMappingStrategy(hSRS, OAMS_TRADITIONAL_GIS_ORDER);
    } else {
        OSRDestroySpatialReference(hSRS);
        return NULL;
    }
    return hSRS;
}

char* projection_wkt_from_nc(const DataNC *meta) {
    OGRSpatialReferenceH hSRS = projection_srs_from_nc(meta);
    if (hSRS == NULL) return NULL;
    char *wkt = NULL;
    OSRExportToWkt(hSRS, &wkt);
    OSRDestroySpatialReference(hSRS);
    return wkt;
}
