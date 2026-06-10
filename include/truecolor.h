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

// Computes the synthetic green channel using the CIMSS formula:
// Green = 0.45*Red + 0.10*NIR + 0.45*Blue
DataF create_truecolor_synthetic_green(const DataF *c_blue, const DataF *c_red, const DataF *c_nir);

// Packs three float grids (R, G, B) into an 8-bit RGB image using per-channel linear stretch.
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

// Applies solar zenith angle correction in-place: reflectance /= cos(SZA).
void apply_solar_zenith_correction(DataF *data, const DataF *sza);

// Applies a piecewise linear contrast stretch in-place to match Geo2grid/Satpy output.
void apply_piecewise_stretch(DataF *band);

// Builds a sharpening ratio map from a reference channel (typically C02 red).
// ratio = channel / mean_2x2(channel), clamped to [0, 1.5]; invalid pixels → 1.0.
// Multiply the ratio map onto other channels to transfer sub-pixel contrast.
DataF dataf_ratio_sharpen_map(const DataF *channel);

#endif // HPSATVIEWS_TRUECOLOR_H
