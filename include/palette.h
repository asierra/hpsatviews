/* Built-in spectral palettes for meteorological visualization.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_PALETA_H_
#define HPSATVIEWS_PALETA_H_

#include "image.h"

/// Palette entry.
typedef struct {
  double d;         ///< Value
  float r, g, b, a; ///< Red, Green, Blue, Alpha
} PaletteData;

/// Meteorological palette for surface and high clouds.
extern PaletteData atmosrainbow[];

/// Converts the meteorological palette to a ColorArray.
ColorArray *atmosrainbow_to_color_array();

/// Creates a typical blue-to-red rainbow palette.
ColorArray *create_rainbow_color_array(unsigned int size);

#endif /* HPSATVIEWS_PALETA_H_ */
