/* WebP image reader (city-lights background layers).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_READER_WEBP_H_
#define HPSATVIEWS_READER_WEBP_H_

#include "image.h"

// Loads a WebP file into an ImageData struct (forced RGBA, 32-bit output).
// Returns an ImageData with data=NULL on failure.
ImageData reader_load_webp(const char *filename);

#endif /* HPSATVIEWS_READER_WEBP_H_ */