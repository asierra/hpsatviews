/* Solar zenith-angle mask for day/night blending in composite modes.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_DAYNIGHT_MASK_H_
#define HPSATVIEWS_DAYNIGHT_MASK_H_

#include "image.h"
#include "datanc.h"

ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo, float *dnratio, float max_temp);

#endif /* HPSATVIEWS_DAYNIGHT_MASK_H_ */
