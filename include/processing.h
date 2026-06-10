/* Single-channel processing pipeline (grayscale and pseudocolor).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_PROCESSING_H_
#define HPSATVIEWS_PROCESSING_H_

#include <stdbool.h>
#include "config.h"
#include "metadata.h"

// Runs the single-channel processing pipeline (gray or pseudocolor).
// Uses immutable ProcessConfig for input and MetadataContext for output.
int run_processing(const ProcessConfig *cfg, MetadataContext *meta);

#endif /* HPSATVIEWS_PROCESSING_H_ */