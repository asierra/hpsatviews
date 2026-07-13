/* GeoTIFF output writer via GDAL (RGB, grayscale, and indexed modes).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_WRITER_GEOTIFF_H_
#define HPSATVIEWS_WRITER_GEOTIFF_H_

#include <stdbool.h>
#include "image.h"
#include "datanc.h"
#include "reader_cpt.h"

/// Physical range and palette size for pseudocolor outputs.
typedef struct {
    float       val_min;
    float       val_max;
    int         num_colors;
    const char *units;      ///< NULL omits colormap_units
    bool        has_nodata; ///< If true, nodata_index is tagged
    int         nodata_index;
} ColormapMeta;

/* `cog` selects the output flavour: false = fast tiled GeoTIFF without overviews
 * (default; ideal for an intermediate that gets cropped downstream); true = full
 * Cloud Optimized GeoTIFF with the overview pyramid (the GeoTIFF is the final
 * product). Both are ZSTD-compressed and written multi-threaded. */

/// Writes a 3-band RGB image to GeoTIFF.
int write_geotiff_rgb(const char* filename,
                      const ImageData* img,
                      const DataNC* meta,
                      int offset_x,
                      int offset_y,
                      const char* product,
                      bool cog);

/// Writes a single-band grayscale image to GeoTIFF.
int write_geotiff_gray(const char* filename,
                       const ImageData* img,
                       const DataNC* meta,
                       int offset_x,
                       int offset_y,
                       const char* product,
                       bool cog);

/// Writes a palette-indexed image to GeoTIFF with embedded color table.
int write_geotiff_indexed(const char* filename,
                          const ImageData* img,
                          const ColorArray* palette,
                          const DataNC* meta,
                          int offset_x,
                          int offset_y,
                          const ColormapMeta* cm,
                          const char* product,
                          bool cog);

#endif /* HPSATVIEWS_WRITER_GEOTIFF_H_ */
