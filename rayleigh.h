/* Rayleigh atmospheric correction for GOES ABI visible bands
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_RAYLEIGH_H_
#define HPSATVIEWS_RAYLEIGH_H_

#include "datanc.h"

// Rayleigh Lookup Table structure (simple version with min/max/step)
typedef struct {
    float *table;       // Flat array with precalculated values
    int n_sz, n_vz, n_az; // Dimensions (Solar Zen, View Zen, Azimuth)
    float sz_min, sz_max, sz_step; // Solar Zenith range
    float vz_min, vz_max, vz_step; // View Zenith range
    float az_min, az_max, az_step; // Relative Azimuth range
} RayleighLUT;

/**
 * @brief Apply Rayleigh atmospheric correction to a reflectance image.
 * 
 * Modifies the input image in-place, subtracting Rayleigh scattering
 * contribution based on viewing geometry.
 * 
 * @param img Input/output reflectance image (modified in-place)
 * @param sza Solar Zenith Angle map (degrees)
 * @param vza View/Satellite Zenith Angle map (degrees)
 * @param raa Relative Azimuth Angle map (degrees)
 * @param lut Pointer to loaded Rayleigh lookup table
 */
void apply_rayleigh_correction(DataF *img, 
                               const DataF *sza, 
                               const DataF *vza, 
                               const DataF *raa, 
                               const RayleighLUT *lut);

/**
 * @brief Load Rayleigh LUT from binary file.
 * 
 * @param filename Path to the LUT binary file
 * @return Loaded RayleighLUT structure
 */
RayleighLUT rayleigh_lut_load(const char *filename);

/**
 * @brief Free memory allocated for a Rayleigh LUT.
 * 
 * @param lut Pointer to the LUT structure to free
 */
void rayleigh_lut_destroy(RayleighLUT *lut);

#endif /* HPSATVIEWS_RAYLEIGH_H_ */
