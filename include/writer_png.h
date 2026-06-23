/* PNG output writer (direct and palette-indexed) via libpng.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_WRITER_PNG_H_
#define HPSATVIEWS_WRITER_PNG_H_

#include "image.h"

/// Writes an RGB/grayscale ImageData to a PNG file.
int writer_save_png(const char *filename, const ImageData *image);

/// Writes a palette-indexed ImageData to PNG.
int writer_save_png_palette(const char *filename, const ImageData *image, const ColorArray *palette);

#endif /* HPSATVIEWS_WRITER_PNG_H_ */