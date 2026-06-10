/* GeoTIFF output writer via GDAL (RGB, grayscale, and indexed modes).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_WRITER_GEOTIFF_H_
#define HPSATVIEWS_WRITER_GEOTIFF_H_

#include "image.h"
#include "datanc.h"
#include "reader_cpt.h"

// Writes a 3-band RGB image to GeoTIFF.
// offset_x/y: pixel offset from the native grid origin (for clipped outputs).
// product: optional descriptive tag written to file metadata; NULL to omit.
// Returns 0 on success, -1 on error.
int write_geotiff_rgb(const char* filename,
                      const ImageData* img,
                      const DataNC* meta,
                      int offset_x,
                      int offset_y,
                      const char* product);

// Writes a single-band grayscale image to GeoTIFF. Returns 0 on success.
int write_geotiff_gray(const char* filename,
                       const ImageData* img,
                       const DataNC* meta,
                       int offset_x,
                       int offset_y);

// Writes a palette-indexed image to GeoTIFF with embedded color table. Returns 0 on success.
int write_geotiff_indexed(const char* filename,
                          const ImageData* img,
                          const ColorArray* palette,
                          const DataNC* meta,
                          int offset_x,
                          int offset_y);

#endif /* HPSATVIEWS_WRITER_GEOTIFF_H_ */
