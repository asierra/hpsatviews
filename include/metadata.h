/* Product metadata aggregation and JSON sidecar serialization.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_METADATA_H_
#define HPSATVIEWS_METADATA_H_

#include <stdbool.h>
#include "datanc.h" 

/// Opaque handle for metadata state.
typedef struct MetadataContext MetadataContext;

/// Allocates an empty metadata context.
MetadataContext* metadata_create(void);

/// Frees the context.
void metadata_destroy(MetadataContext *ctx);

void metadata_add_int(MetadataContext *ctx, const char *key, int value);
void metadata_add_dbl(MetadataContext *ctx, const char *key, double value);
void metadata_add_str(MetadataContext *ctx, const char *key, const char *value);
void metadata_add_bool(MetadataContext *ctx, const char *key, bool value);

/// C11 _Generic polymorphic insertion.
#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        bool:         metadata_add_bool, \
        int:          metadata_add_int, \
        float:        metadata_add_dbl, \
        double:       metadata_add_dbl, \
        char*:        metadata_add_str, \
        const char*:  metadata_add_str \
    )(CTX, KEY, VAL)

/// Sets the processing command/mode string.
void metadata_set_command(MetadataContext *ctx, const char *command);

/// Sets a descriptive product name.
void metadata_set_product(MetadataContext *ctx, const char *product);

void metadata_set_projection(MetadataContext *ctx, const char *proj);

/// Marks the output as user-clipped.
void metadata_set_clip(MetadataContext *ctx, bool clipped);

/// Records the final image bounding box (lon_min, lat_max, lon_max, lat_min).
void metadata_set_geometry(MetadataContext *ctx, float x1, float y1, float x2, float y2);

/// Canonical satellite name ("G16") for an identifier; "unknown" if out of range.
const char* metadata_sat_name(SatelliteID id);

/// Canonical sector name ("fd", "conus", "m1", "m2"); empty string if unknown.
const char* metadata_sector_name(SectorID id);

/// Populates metadata from a loaded DataNC.
void metadata_from_nc(MetadataContext *ctx, const DataNC *nc);

/// Builds a standardized output filename. Caller must free the returned string.
char* metadata_build_filename(const MetadataContext *ctx, const char *extension);

/// Serializes metadata to a JSON file.
int metadata_save_json(MetadataContext *ctx, const char *filename);

#endif /* HPSATVIEWS_METADATA_H_ */
