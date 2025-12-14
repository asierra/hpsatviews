/*
 * RGB and day/night composite generation module.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_RGB_H_
#define HPSATVIEWS_RGB_H_
#include "args.h"
#include "image.h" // Necesario para ImageData
#include "datanc.h" // Necesario para DataF
#include <stdbool.h>

int run_rgb(ArgParser* parser);

ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

// True color RGB generation (basic version without Rayleigh)
ImageData create_truecolor_rgb(DataF c01_blue, DataF c02_red, DataF c03_nir);

// True color RGB generation with optional Rayleigh atmospheric correction
ImageData create_truecolor_rgb_rayleigh(DataF c01_blue, DataF c02_red, DataF c03_nir,
                                        const char *filename_ref, bool apply_rayleigh);

#endif /* HPSATVIEWS_RGB_H_ */