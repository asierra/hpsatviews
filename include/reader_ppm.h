/* Binary PPM (P6) image reader (city-lights background layers).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_READER_PPM_H_
#define HPSATVIEWS_READER_PPM_H_

#include "image.h"

// Loads an 8-bit binary PPM (P6) into an RGB ImageData (bpp 3).
// Returns an ImageData with data=NULL on failure.
ImageData reader_load_ppm(const char *filename);

#endif /* HPSATVIEWS_READER_PPM_H_ */
