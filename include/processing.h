/*
 * Single-channel processing module (gray and pseudocolor)
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_PROCESSING_H_
#define HPSATVIEWS_PROCESSING_H_

#include <stdbool.h>
#include "config.h"
#include "metadata.h"

/**
 * Procesamiento de un solo canal (gray y pseudocolor).
 * Usa ProcessConfig (inmutable) y MetadataContext (mutable).
 */
int run_processing(const ProcessConfig *cfg, MetadataContext *meta);

#endif /* HPSATVIEWS_PROCESSING_H_ */