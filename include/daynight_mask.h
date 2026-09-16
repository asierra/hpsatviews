/* Solar zenith-angle mask for day/night blending in composite modes.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_DAYNIGHT_MASK_H_
#define HPSATVIEWS_DAYNIGHT_MASK_H_

#include "image.h"
#include "datanc.h"


#include <time.h>

/* Day/night blending limits, shared with src/cuda/daynite_cuda.cu (which pulls
 * this header in through cuda_daynite.h). These are satpy's DayNightCompositor
 * defaults, lim_low=85 / lim_high=88: full day below 85 deg of solar zenith,
 * full night above 88, linear in between. The blend runs on sin(elevation),
 * which is identically cos(SZA), the same variable satpy interpolates on.
 * HPSV_DN_TERMINATOR must not exceed HPSV_SUNZ_LIMIT or the day side would be
 * blended in where apply_solar_zenith_correction() has already zeroed it. */
#define HPSV_DN_TERMINATOR 88.0f
#define HPSV_DN_PENUMBRA    3.0f

/// Efeméride solar dependiente solo del tiempo (constante para toda la imagen).
/// Se expone para que el kernel CUDA de la máscara reciba los mismos escalares
/// que usa la ruta CPU, en vez de recalcularlos con otra implementación.
typedef struct {
    double t;               ///< Julian time parameter
    double RightAscension;
    double sd;              ///< sin(Declination)
    double cd;              ///< cos(Declination)
    double Dlam;
    double hour_angle_base; ///< 1.7528311 + 6.300388099*t - RightAscension + 0.92*Dlam
} SolarEphemeris_dn;

SolarEphemeris_dn solar_ephemeris_precompute(time_t timestamp);

ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo, float *dnratio, float max_temp);

#endif /* HPSATVIEWS_DAYNIGHT_MASK_H_ */
