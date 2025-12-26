/*
 * Single-channel processing module (gray and pseudocolor)
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_PROCESSING_H_
#define HPSATVIEWS_PROCESSING_H_

#include "args.h"
int run_processing(ArgParser *parser, bool is_pseudocolor);
#endif /* HPSATVIEWS_PROCESSING_H_ */