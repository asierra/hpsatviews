/* True-color and multi-band RGB image generation for ABI composites.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_TRUECOLOR_H
#define HPSATVIEWS_TRUECOLOR_H

#include "datanc.h"
#include "image.h"
#include <stdbool.h>

/// Computes the synthetic green channel using the CIMSS formula.
DataF create_truecolor_synthetic_green(const DataF *c_blue, const DataF *c_red, const DataF *c_nir);

/// Packs three float grids (R, G, B) into an 8-bit RGB image using per-channel linear stretch.
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

/// Applies solar zenith angle correction in-place.
void apply_solar_zenith_correction(DataF *data, const DataF *sza);

/// Applies a piecewise linear contrast stretch in-place to match Geo2grid/Satpy output.
void apply_piecewise_stretch(DataF *band);

/// Curva del stretch piecewise de geo2grid (5 puntos, interpolación lineal entre
/// ellos). Expuesta para que el kernel CUDA use exactamente la misma tabla en vez
/// de una copia: la equivalencia CPU/GPU no debe depender de mantener dos
/// literales sincronizados a mano.
#define HPSV_STRETCH_COUNT 5
extern const float GEO2GRID_STRETCH_X[HPSV_STRETCH_COUNT];
extern const float GEO2GRID_STRETCH_Y[HPSV_STRETCH_COUNT];

/// Builds a sharpening ratio map from a reference channel.
DataF dataf_ratio_sharpen_map(const DataF *channel);

#endif // HPSATVIEWS_TRUECOLOR_H
