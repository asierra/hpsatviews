/* Device-resident viewing geometry for Rayleigh: computes solar zenith (SZA),
 * view zenith (VZA) and relative azimuth (RAA) from lat/lon grids already on
 * the GPU, so the ~8 s of per-pixel astronomy leaves the CPU.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Solo bajo #ifdef HPSV_CUDA; implementación en src/cuda/nav_cuda.cu.
 */
#ifndef HPSATVIEWS_CUDA_NAV_H_
#define HPSATVIEWS_CUDA_NAV_H_

#include "cuda_dataf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Computa sza/vza/raa en device a partir de lat/lon (ya en device) y los
 * parámetros escalares leídos en host: la efeméride solar (sd/cd/ha_base, ver
 * SolarEphemeris en reader_nc.h) y los del satélite (sub-punto y altitud).
 * Réplica exacta de compute_solar_angles_nc + compute_satellite_angles_nc +
 * compute_relative_azimuth (src/reader_nc.c). Reserva sza_out/vza_out/raa_out
 * (el llamador los libera con dataf_dev_destroy). Devuelve false ante fallo. */
bool compute_rayleigh_nav_dev(const DataFDev *navla, const DataFDev *navlo,
                              double sd, double cd, double ha_base,
                              float sat_lon, float sat_height_m,
                              DataFDev *sza_out, DataFDev *vza_out,
                              DataFDev *raa_out);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_NAV_H_ */
