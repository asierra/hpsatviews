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
 * this header in through cuda_daynite.h): full day below 75 deg of solar
 * zenith, full night above 85, linear in between. The blend runs on
 * sin(elevation), which is identically cos(SZA), the same variable satpy's
 * DayNightCompositor interpolates on.
 *
 * These are deliberately NOT satpy's defaults (lim_low=85, lim_high=88), even
 * though the rest of the terminator handling was aligned with satpy in v1.2.0
 * and the aligned values were measured and found cheap. Moving the blend to
 * 85-88 replaces the nocturnal IR composite with true colour across a band ten
 * degrees wide, and with it the cloud-top temperature gradient that the night
 * side colour-codes: on a GOES-19 full disk at dusk an entire frontal system
 * went from a blue-to-yellow thermal structure to a featureless white mass.
 * --cloud-temp does not restore it, because it is a global threshold rather
 * than a terminator-local one: -T 250 recovers the structure at dusk but also
 * scatters IR patches over the whole sunlit disk (19.65 % of it differs from
 * the 75-85 rendering, against 1.42 % for these limits). Whether that trade is
 * acceptable is a forecasting decision, not a correctness one, so the
 * operational rendering stays put until someone makes it.
 *
 * HPSV_DN_TERMINATOR must not exceed HPSV_SUNZ_LIMIT: past that the blend would
 * pull in a day side that apply_solar_zenith_correction() has already faded,
 * and a seam appears. Raising it to 88 is what the satpy-aligned variant did,
 * and it works — that direction is available; lowering HPSV_SUNZ_LIMIT is not. */
#define HPSV_DN_TERMINATOR 85.0f
#define HPSV_DN_PENUMBRA   10.0f

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
