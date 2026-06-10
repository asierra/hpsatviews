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

// Opaque handle for metadata state (hides JSON backend).
typedef struct MetadataContext MetadataContext;

// Lifecycle

// Allocates an empty metadata context.
MetadataContext* metadata_create(void);

// Frees the context. Safe to call with NULL.
void metadata_destroy(MetadataContext *ctx);

// Data insertion API

void metadata_add_int(MetadataContext *ctx, const char *key, int value);
void metadata_add_dbl(MetadataContext *ctx, const char *key, double value);
void metadata_add_str(MetadataContext *ctx, const char *key, const char *value);
void metadata_add_bool(MetadataContext *ctx, const char *key, bool value);

// C11 _Generic polymorphic insertion: metadata_add(ctx, "gamma", 1.5);
#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        bool:         metadata_add_bool, \
        int:          metadata_add_int, \
        float:        metadata_add_dbl, \
        double:       metadata_add_dbl, \
        char*:        metadata_add_str, \
        const char*:  metadata_add_str \
    )(CTX, KEY, VAL)

// Domain-specific setters

// Sets the processing command/mode string.
void metadata_set_command(MetadataContext *ctx, const char *command);

// Sets a descriptive product name (e.g., "True Color RGB").
void metadata_set_product(MetadataContext *ctx, const char *product);

void metadata_set_projection(MetadataContext *ctx, const char *proj);

// Marks the output as user-clipped.
void metadata_set_clip(MetadataContext *ctx, bool clipped);

// Records the final image bounding box: x1=lon_min, y1=lat_max, x2=lon_max, y2=lat_min.
void metadata_set_geometry(MetadataContext *ctx, float x1, float y1, float x2, float y2);

// Populates metadata from a loaded DataNC (satellite, band, timestamp, projection).
void metadata_from_nc(MetadataContext *ctx, const DataNC *nc);

// Output generation

// Builds a standardized output filename from accumulated metadata.
// extension: ".png" or ".json". Caller must free the returned string.
char* metadata_build_filename(const MetadataContext *ctx, const char *extension);

// Serializes metadata to a JSON file. Returns 0 on success.
int metadata_save_json(MetadataContext *ctx, const char *filename);

#endif /* HPSATVIEWS_METADATA_H_ */
