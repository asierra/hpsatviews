/*
 * True color and multiband RGB image generation
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_TRUECOLOR_H
#define HPSATVIEWS_TRUECOLOR_H

#include "datanc.h"
#include "image.h"
#include <stdbool.h>

/**
 * @brief Genera SOLO el canal verde sintético.
 * Formula CIMSS: Green = 0.45*Red + 0.10*NIR + 0.45*Blue
 */
DataF create_truecolor_synthetic_green(const DataF *c_blue, const DataF *c_red, const DataF *c_nir);


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

                               
/**
 * @brief Aplica corrección de ángulo cenital solar a datos de reflectancia.
 * 
 * Formula: reflectance_corrected = reflectance_TOA / cos(solar_zenith_angle)
 * 
 * @param data Datos de reflectancia (in-place modification)
 * @param sza Ángulos cenitales solares en grados
 */
void apply_solar_zenith_correction(DataF *data, const DataF *sza);


/**
 * @brief Aplica un estiramiento de contraste lineal por tramos (Piecewise Linear Stretch).
 * Usado para replicar el "look" de Geo2grid/Satpy.
 * * @param band Banda a realzar (modificada in-place)
 */                               
void apply_piecewise_stretch(DataF *band);

#endif // HPSATVIEWS_TRUECOLOR_H
