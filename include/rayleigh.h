/* Rayleigh atmospheric correction for GOES ABI visible bands
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_RAYLEIGH_H_
#define HPSATVIEWS_RAYLEIGH_H_

#include "datanc.h"
#include <stdbool.h>

typedef struct {
    DataF sza; // Solar Zenith Angle
    DataF vza; // View Zenith Angle
    DataF raa; // Relative Azimuth Angle
} RayleighNav;


// Analytic Rayleigh correction using physical scattering formula (no LUT required).
// Corrects band in-place. lambda_um: central wavelength in micrometres.
void analytic_rayleigh_correction(DataF *band, const RayleighNav *nav, float lambda_um);

// Loads viewing geometry (SZA, VZA, RAA) from an L1b NetCDF file into nav.
// Resamples geometry grids to target_width x target_height.
bool rayleigh_load_navigation(const char *filename, RayleighNav *nav, 
				unsigned int target_width, unsigned int target_height);

// Variant of rayleigh_load_navigation that reuses pre-computed lat/lon grids.
bool rayleigh_load_navigation_from_latlon(const char *filename,
                                          const DataF *navla, const DataF *navlo,
                                          RayleighNav *nav,
                                          unsigned int target_width,
                                          unsigned int target_height);


// Rayleigh Lookup Table structure (simple version with min/max/step)
typedef struct {
    float *table;       // Flat array with precalculated values
    int n_sz, n_vz, n_az; // Dimensions (Solar Zen, View Zen, Azimuth)
    float sz_min, sz_max, sz_step; // Solar Zenith range
    float vz_min, vz_max, vz_step; // View Zenith range
    float az_min, az_max, az_step; // Relative Azimuth range
} RayleighLUT;

// Frees geometry grids inside a RayleighNav.
void rayleigh_free_navigation(RayleighNav *nav);

// LUT-based Rayleigh correction. Corrects img in-place using pre-computed scattering tables.
// channel: ABI band index (1=C01, 2=C02, 3=C03); redband: C01 reflectance for aerosol correction.
void luts_rayleigh_correction(DataF *img, const RayleighNav *nav, const uint8_t channel, const DataF *redband);

void rayleigh_lut_destroy(RayleighLUT *lut);

#endif /* HPSATVIEWS_RAYLEIGH_H_ */
