/* Internal palettes.
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_PALETA_H_
#define HPSATVIEWS_PALETA_H_

#include "image.h"

// Value, R, G, B, Alpha
typedef struct {
    double d;
    float r, g, b, a;
} PaletaData;


//  Meteorological palette for surface and high clouds
extern PaletaData atmosrainbow[];

ColorArray* atmosrainbow_to_color_array();

// Typical rainbow Blue-to-Red
ColorArray *create_rainbow_color_array(unsigned int size);

#endif /* HPSATVIEWS_PALETA_H_ */

