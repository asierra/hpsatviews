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

/// Tramo de atmosrainbow que contiene @p f: el t con d[t] <= f < d[t+1].
///
/// Búsqueda binaria con la misma semántica que el recorrido lineal de
/// create_nocturnal_pseudocolor(), incluidos sus bordes: fuera de [d[0], d[255])
/// —y con NaN, donde ninguna comparación es verdadera— devuelve 254. La
/// equivalencia descansa en que los umbrales son estrictamente crecientes; no
/// están espaciados uniformemente (de 0.81 a 3 K), así que un índice directo
/// no daría el mismo tramo.
static inline unsigned int atmosrainbow_index(float f) {
  if (!(f >= atmosrainbow[0].d && f < atmosrainbow[255].d)) return 254;
  unsigned int lo = 0, hi = 255;
  while (hi - lo > 1) {
    unsigned int mid = (lo + hi) / 2;
    if (f >= atmosrainbow[mid].d) lo = mid;
    else hi = mid;
  }
  return lo;
}

/// Converts the meteorological palette to a ColorArray.
ColorArray *atmosrainbow_to_color_array();

/// Creates a typical blue-to-red rainbow palette.
ColorArray *create_rainbow_color_array(unsigned int size);

#endif /* HPSATVIEWS_PALETA_H_ */
