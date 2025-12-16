/*
 * True color and multiband RGB image generation
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_TRUECOLOR_H
#define HPSATVIEWS_TRUECOLOR_H

#include "datanc.h"
#include "image.h"
#include <stdbool.h>

/**
 * @brief Crea una imagen True Color RGB a partir de los canales azul, rojo y NIR.
 */
ImageData create_truecolor_rgb(DataF c01_blue, DataF c02_red, DataF c03_nir);

/**
 * @brief Crea una imagen True Color RGB con corrección atmosférica de Rayleigh opcional.
 */
ImageData create_truecolor_rgb_rayleigh(DataF c01_blue, DataF c02_red, DataF c03_nir,
                                        const char *filename_ref, bool apply_rayleigh);

/**
 * @brief Crea una imagen RGB a partir de tres mallas de datos flotantes (DataF).
 *
 * Normaliza cada canal de entrada según sus rangos (min/max) y los mapea a
 * los canales R, G y B de la imagen de salida.
 *
 * @return Una estructura ImageData con la imagen RGB resultante.
 */
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);


#endif // HPSATVIEWS_TRUECOLOR_H
