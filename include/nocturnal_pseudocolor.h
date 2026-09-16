/* Nighttime pseudocolor imagery from ABI C13 brightness temperature.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_NOCTURNAL_PSEUDOCOLOR_H_
#define HPSATVIEWS_NOCTURNAL_PSEUDOCOLOR_H_

#include "image.h"
#include "datanc.h"

/// Generates a nighttime pseudocolor image from ABI C13 brightness temperature, optionally blended with city lights.
ImageData create_nocturnal_pseudocolor(const DataF* temp_data, const ImageData* fondo);

/// PROTOTIPO: superpone la paleta infrarroja sobre @p base con una rampa lineal
/// entre @p t_opaque (infrarrojo puro) y @p t_clear (base intacta).
void image_overlay_ir(ImageData *base, const DataF *temp, float t_opaque, float t_clear);

#endif /* HPSATVIEWS_NOCTURNAL_PSEUDOCOLOR_H_ */
