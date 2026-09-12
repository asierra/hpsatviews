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
#include "footprint.h"

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

/// Records the final image bounding box as (x_min, y_min, x_max, y_max), i.e.
/// [W, S, E, N] — the GeoJSON and STAC order, which is what the three callers
/// already pass. Units follow the projection: degrees once reprojected
/// (EPSG:4326), metres on the satellite's fixed grid.
void metadata_set_geometry(MetadataContext *ctx, double x1, double y1, double x2, double y2);

/// Records the geographic footprint (EPSG:4326) of the output: the ring that
/// follows the raster edge and the Earth's limb, plus its bbox. Independent of
/// metadata_set_geometry(), which keeps describing the output's own CRS.
void metadata_set_footprint(MetadataContext *ctx, const Footprint *fp);

/// Records the grid of the output raster: its affine transform in the units of
/// its own CRS, in the order STAC's proj:transform uses (pixel width, row
/// rotation, origin x, column rotation, pixel height, origin y — NOT GDAL's),
/// its shape as [height, width], and the CRS as WKT2. `epsg` is 4326 once
/// reprojected and 0 (meaning "no authority code") on the fixed grid.
void metadata_set_grid(MetadataContext *ctx, const double transform[6],
                       int width, int height, const char *wkt2, int epsg);

/// Canonical satellite name ("G16") for an identifier; "unknown" if out of range.
const char* metadata_sat_name(SatelliteID id);

/// Canonical sector name ("fd", "conus", "m1", "m2"); empty string if unknown.
const char* metadata_sector_name(SectorID id);

/// Populates metadata from a loaded DataNC.
void metadata_from_nc(MetadataContext *ctx, const DataNC *nc);

/// Media type of an output, for the Item's assets.
const char* metadata_media_type(bool is_geotiff, bool cog);

/// Registers one written output as an asset of the Item. `key` is the asset
/// key ("image", "image_geographic"): the operation goes in the key, per D1 of
/// docs/stac/STAC_PLAN.md. Paths do NOT go through metadata_add_str(), which
/// truncates at 63 characters.
/// `transform` may be NULL. It is per-asset on purpose: a -B run writes two
/// rasters on different grids, and the Item's own proj:transform can only
/// describe one of them, so a client would otherwise georeference the other
/// one wrong.
void metadata_add_asset(MetadataContext *ctx, const char *key, const char *href,
                        const char *media_type, int width, int height,
                        const double transform[6], int epsg);

/// Stable Item id: the scene plus the product, with no enhancement segment.
/// metadata_build_filename() encodes gamma, CLAHE and clipping in the name, so
/// it cannot serve as an identifier — two renderings of one scene would be two
/// items. Caller must free.
char* metadata_build_id(const MetadataContext *ctx);

/// Builds a standardized output filename. Caller must free the returned string.
char* metadata_build_filename(const MetadataContext *ctx, const char *extension);

/// Serializes the metadata as a STAC Item to a JSON file. `collection` may be
/// NULL, and then the Item carries no `collection`: valid STAC, and the
/// indexer assigns it on ingest.
int metadata_save_stac_item(MetadataContext *ctx, const char *filename,
                            const char *collection);

#endif /* HPSATVIEWS_METADATA_H_ */
