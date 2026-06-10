/* Single-channel grayscale and pseudocolor image generation.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_GRAY_H_
#define HPSATVIEWS_GRAY_H_

#include "image.h"
#include "datanc.h"
#include "reader_cpt.h"
#include <stdbool.h>

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha,
                             float min_val, float max_val, const CPTData* cpt);

ImageData create_single_grayb(DataB c01, bool invert_value, bool use_alpha, const CPTData* cpt);

#endif /* HPSATVIEWS_GRAY_H_ */
