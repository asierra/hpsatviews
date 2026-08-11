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
#include "nav_plan.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Computa la malla lat/lon directamente en device desde el plan de proyección,
 * evitando calcularla en CPU (~0.15 s en un disco completo) y subir dos grids de
 * ~450 MB. Réplica del bucle de compute_navigation_nc() en la misma aritmética
 * double, con el mismo precómputo de sin/cos por eje.
 *
 * Devuelve además el min/max de lat y lon, que es lo único que la ruta host
 * seguía necesitando de la navegación: fija la extensión del reproyectado.
 * Reserva lat_out/lon_out (el llamador los libera con dataf_dev_destroy).
 * Devuelve false ante fallo, sin reservar nada. */
bool compute_navigation_dev(const NavPlan *plan, DataFDev *lat_out,
                            DataFDev *lon_out, float *lat_min, float *lat_max,
                            float *lon_min, float *lon_max);

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
