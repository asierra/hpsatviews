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
int run_rgb(ArgParser* parser);
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);
#endif /* HPSATVIEWS_RGB_H_ */