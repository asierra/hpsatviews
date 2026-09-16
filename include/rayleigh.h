/* Rayleigh atmospheric correction for GOES-R ABI visible bands.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_RAYLEIGH_H_
#define HPSATVIEWS_RAYLEIGH_H_

#include "datanc.h"
#include <stdbool.h>

/* Terminator limits, shared by the OpenMP and CUDA paths so the two cannot
 * drift (src/cuda/rayleigh_cuda.cu includes this header). The values are
 * satpy's: SunZenithCorrector.correction_limit and SunZenithCorrectorBase
 * .max_sza, which is what geo2grid runs with — polar2grid's abi.yaml does not
 * override them. Past HPSV_SUNZ_LIMIT the 1/cos gain is capped at its value
 * there and faded out logarithmically, reaching zero at HPSV_SUNZ_MAX_SZA. */
#define HPSV_SUNZ_LIMIT    88.0f
#define HPSV_SUNZ_MAX_SZA  95.0f

/* Solar zenith angle at which the Rayleigh correction starts fading out; it
 * reaches zero at HPSV_SUNZ_MAX_SZA, so the fade covers exactly the range that
 * still gets rendered. */
#define HPSV_RAY_TAPER_LOW 70.0f

/* Hard ceiling of the pyspectral LUT, arccos(1/24.75): angles above this are
 * evaluated at this value, as pyspectral itself does. Not a rendering limit. */
#define HPSV_RAY_LUT_SZA_MAX 87.68f

typedef struct {
    DataF sza; ///< Solar Zenith Angle
    DataF vza; ///< View Zenith Angle
    DataF raa; ///< Relative Azimuth Angle
} RayleighNav;


/// Analytic Rayleigh correction using physical scattering formula.
void analytic_rayleigh_correction(DataF *band, const RayleighNav *nav, float lambda_um);

/// Loads viewing geometry from an L1b NetCDF file and resamples to target dimensions.
/// @param meta Already-loaded metadata for @p filename, or NULL to read it from
///        the file again. Passing it avoids two reopens per scene (see
///        compute_solar_angles_at); a NULL or incomplete @p meta is not an error,
///        the file is simply read as before.
bool rayleigh_load_navigation(const char *filename, const DataNC *meta, RayleighNav *nav,
				unsigned int target_width, unsigned int target_height);

/// Loads viewing geometry reusing pre-computed lat/lon grids.
bool rayleigh_load_navigation_from_latlon(const char *filename, const DataNC *meta,
                                          const DataF *navla, const DataF *navlo,
                                          RayleighNav *nav,
                                          unsigned int target_width,
                                          unsigned int target_height);


/// Rayleigh Lookup Table structure.
typedef struct {
    float *table;                  ///< Flat array with precalculated values
    int n_sz, n_vz, n_az;          ///< Dimensions (Solar Zen, View Zen, Azimuth)
    float sz_min, sz_max, sz_step; ///< Solar Zenith range
    float vz_min, vz_max, vz_step; ///< View Zenith range
    float az_min, az_max, az_step; ///< Relative Azimuth range
} RayleighLUT;

/// Frees geometry grids inside a RayleighNav.
void rayleigh_free_navigation(RayleighNav *nav);

/// LUT-based Rayleigh correction.
void luts_rayleigh_correction(DataF *img, const RayleighNav *nav, const uint8_t channel, const DataF *redband);

/// Loads the embedded Rayleigh LUT for an ABI channel (table is NULL on failure).
/// Exposed so the CUDA path can reuse the exact same LUT parsing.
RayleighLUT rayleigh_lut_load_from_memory(const uint8_t channel);

void rayleigh_lut_destroy(RayleighLUT *lut);

#endif /* HPSATVIEWS_RAYLEIGH_H_ */
