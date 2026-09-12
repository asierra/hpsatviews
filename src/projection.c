/* Shared construction of the output spatial reference system.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "projection.h"
#include "logger.h"
#include <cpl_conv.h>
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

char* projection_wkt2_from_nc(const DataNC *meta) {
    OGRSpatialReferenceH hSRS = projection_srs_from_nc(meta);
    if (hSRS == NULL) return NULL;
    char *wkt = NULL;
    const char *opts[] = {"FORMAT=WKT2_2019", "MULTILINE=NO", NULL};
    if (OSRExportToWktEx(hSRS, &wkt, opts) != OGRERR_NONE) {
        OSRDestroySpatialReference(hSRS);
        return NULL;
    }
    OSRDestroySpatialReference(hSRS);
    return wkt;
}

void projection_output_geotransform(const double src_gt[6], unsigned crop_x,
                                    unsigned crop_y, int scale, double out_gt[6]) {
    if (src_gt == NULL || out_gt == NULL) return;
    memcpy(out_gt, src_gt, 6 * sizeof(double));

    // The crop shift uses the ORIGINAL pixel size, so it has to come before the
    // resampling stretch; swapping the two misplaces the origin by the scale
    // factor.
    out_gt[0] += crop_x * out_gt[1];
    out_gt[3] += crop_y * out_gt[5];

    if (scale != 1 && scale != 0) {
        const double sf = (scale < 0) ? -(double)scale : (double)scale;
        if (scale > 1) { out_gt[1] /= sf; out_gt[5] /= sf; }
        else           { out_gt[1] *= sf; out_gt[5] *= sf; }
    }
}

void projection_stac_transform(const DataNC *meta, unsigned crop_x, unsigned crop_y,
                               int scale, double stac[6]) {
    if (meta == NULL || stac == NULL) return;
    double gt[6];
    projection_output_geotransform(meta->geotransform, crop_x, crop_y, scale, gt);

    if (meta->proj_code == PROJ_GEOS && meta->proj_info.valid) {
        const double h = meta->proj_info.sat_height;
        for (int i = 0; i < 6; i++) gt[i] *= h;
    }

    stac[0] = gt[1];  /* pixel width      */
    stac[1] = gt[2];  /* row rotation     */
    stac[2] = gt[0];  /* origin x         */
    stac[3] = gt[4];  /* column rotation  */
    stac[4] = gt[5];  /* pixel height     */
    stac[5] = gt[3];  /* origin y         */
}

void projection_free_wkt(char *wkt) {
    if (wkt != NULL) CPLFree(wkt);
}
