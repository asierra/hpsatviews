/* Solar computing to create a day/night mask.
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "image.h"
#include "logger.h"
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


// Time-dependent solar ephemeris (constant for all pixels in one image)
typedef struct {
    double t;               // Julian time parameter
    double RightAscension;
    double sd;              // sin(Declination)
    double cd;              // cos(Declination)
    double Dlam;
    double hour_angle_base; // 1.7528311 + 6.300388099*t - RightAscension + 0.92*Dlam
} SolarEphemeris;

static SolarEphemeris solar_ephemeris_precompute(time_t timestamp) {
    SolarEphemeris eph;
    struct tm ts;
    gmtime_r(&timestamp, &ts);
    int Year = ts.tm_year + 1900;
    int Month = ts.tm_mon + 1;
    int Day = ts.tm_mday;
    double UT = ts.tm_hour + (ts.tm_min / 60.0) + (ts.tm_sec / 3600.0);

    int yt, mt;
    if (Month <= 2) {
        mt = Month + 12;
        yt = Year - 1;
    } else {
        mt = Month;
        yt = Year;
    }

    double t = (double)((int)(365.25 * (double)(yt - 2000)) + (int)(30.6001 * (double)(mt + 1)) -
                        (int)(0.01 * (double)(yt)) + Day) +
               0.0416667 * UT - 21958.0;
    double Dt = 96.4 + 0.00158 * t;
    double te = t + 1.1574e-5 * Dt;

    double wte = 0.0172019715 * te;
    double s1 = sin(wte);
    double c1 = cos(wte);
    double s2 = 2.0 * s1 * c1;
    double c2 = (c1 + s1) * (c1 - s1);
    double s3 = s2 * c1 + c2 * s1;
    double c3 = c2 * c1 - s2 * s1;

    double L = 1.7527901 + 1.7202792159e-2 * te + 3.33024e-2 * s1 - 2.0582e-3 * c1 +
               3.512e-4 * s2 - 4.07e-5 * c2 + 5.2e-6 * s3 - 9e-7 * c3 -
               8.23e-5 * s1 * sin(2.92e-5 * te) +
               1.27e-5 * sin(1.49e-3 * te - 2.337) + 1.21e-5 * sin(4.31e-3 * te + 3.065) +
               2.33e-5 * sin(1.076e-2 * te - 1.533) + 3.49e-5 * sin(1.575e-2 * te - 2.358) +
               2.67e-5 * sin(2.152e-2 * te + 0.074) + 1.28e-5 * sin(3.152e-2 * te + 1.547) +
               3.14e-5 * sin(2.1277e-1 * te - 0.488);

    double nu = 9.282e-4 * te - 0.8;
    double Dlam = 8.34e-5 * sin(nu);
    double lambda = L + M_PI + Dlam;
    double epsi = 4.089567e-1 - 6.19e-9 * te + 4.46e-5 * cos(nu);

    double sl = sin(lambda);
    double cl = cos(lambda);
    double se = sin(epsi);
    double ce = sqrt(1 - se * se);

    double RightAscension = atan2(sl * ce, cl);
    if (RightAscension < 0.0)
        RightAscension += 2 * M_PI;

    double Declination = asin(sl * se);

    eph.t = t;
    eph.RightAscension = RightAscension;
    eph.sd = sin(Declination);
    eph.cd = sqrt(1 - eph.sd * eph.sd);
    eph.Dlam = Dlam;
    eph.hour_angle_base = 1.7528311 + 6.300388099 * t - RightAscension + 0.92 * Dlam;

    return eph;
}

// Per-pixel zenith computation using precomputed ephemeris
static inline double sun_zenith_fast(float la, float lo, const SolarEphemeris *eph) {
    double Longitude = lo * (M_PI / 180.0);
    double Latitude = la * (M_PI / 180.0);

    double HourAngle = eph->hour_angle_base + Longitude;
    HourAngle = fmod(HourAngle + M_PI, 2 * M_PI) - M_PI;
    if (HourAngle < -M_PI)
        HourAngle += 2 * M_PI;

    double sp = sin(Latitude);
    double cp = sqrt(1 - sp * sp);
    double cH = cos(HourAngle);
    double se0 = sp * eph->sd + cp * eph->cd * cH;
    double ep = asin(se0) - 4.26e-5 * sqrt(1.0 - se0 * se0);

    double De;
    if (ep > 0.0)
        De = (0.08422 * 1.0) / (273.0 * tan(ep + 0.003138 / (ep + 0.08919)));
    else
        De = 0.0;

    return M_PI_2 - ep - De;
}

// All data structures must be of the same dimensions, or the result will be wrong.
ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo, float *dnratio,
                               float max_temp) {
    ImageData imout = image_create(navla.width, navla.height, 1);

    // Check if allocation was successful
    if (imout.data == NULL) {
        return imout;
    }

    unsigned long day, nite;
    day = nite = 0;

    double start = omp_get_wtime();
    float *navla_data = navla.data_in;
    float *navlo_data = navlo.data_in;
    float *temp_data = datanc.fdata.data_in;
    unsigned char *imout_data = imout.data;

    float terminador = 85;
    float penumbra = 10;

    // Precompute time-dependent solar ephemeris ONCE (was repeated per pixel)
    SolarEphemeris eph = solar_ephemeris_precompute(datanc.timestamp);

#pragma omp parallel for shared(navla_data, navlo_data, temp_data, imout_data)                     \
    reduction(+ : day, nite)
    for (unsigned y = 0; y < navla.height; y++) {
        for (unsigned x = 0; x < navla.width; x++) {
            int i = y * navla.width + x;
            int po = i * imout.bpp;

            float w = 0;
            float la = navla_data[i];
            float lo = navlo_data[i];
            float temp = temp_data[i];
            double sza = sun_zenith_fast(la, lo, &eph) * 180 / M_PI;
            if (sza > terminador) { // Nite
                w = 1;
                nite++;
            } else if (terminador-penumbra < sza && sza < terminador) { // Twilight
                w = (sza - (terminador-penumbra)) / penumbra;
                if (w >= 0.5)
                    nite++;
                else
                    day++;
            } else { // Day
                w = 0;
                day++;
            }
            // Even if it is day, it is opaque for high clouds
            if (temp < max_temp) {
                w = 1;
            }
            imout_data[po] = (unsigned char)(255 * w);
        }
    }
    *dnratio = (nite == 0) ? 100 : 100.0 * day / navla.size;
    double end = omp_get_wtime();
    LOG_DEBUG("Máscara día/noche: %.3f s", end - start);

    return imout;
}
