/* Error de la geometria de vision en float frente a la de produccion.
 *
 * Por que existe: el kernel de geometria de src/cuda/nav_cuda.cu corre en float
 * y la ruta CPU en double. Este programa mide cuanto cuesta eso en el producto.
 *
 * Produccion (referencia): navegacion en double guardada en float (DataF),
 * geometria solar y del satelite en double a partir de esa lat/lon float
 * (sun_angles_from_ephemeris y compute_satellite_view_angles de
 * src/reader_nc.c, copiadas aqui con el tipo como parametro).
 *
 * Variantes:
 *   G   : misma lat/lon float, geometria por pixel en float. La efemeride por
 *         escena se calcula en double en el host y ha_base se reduce mod 2*pi
 *         antes de pasarla como float (lo que hace el kernel).
 *   NG  : navegacion en float + geometria en float: la cadena float completa.
 *   Gn  : como G pero con ha_base sin reducir (float ingenuo), como trampa.
 *
 * Impacto en el producto: error de la ganancia de cenit solar de
 * apply_solar_zenith_correction() (src/truecolor.c) y de sec(VZA), indice de
 * la LUT de Rayleigh, en DN lineales sobre 255 (cota con reflectancia a escala
 * completa).
 *
 * La malla es la de GOES-19 a 0.5 km y la hora la de la escena del 11 de agosto
 * de 2026, 18:00:21 UTC; ambas estan fijas abajo.
 *
 * Compilar y ejecutar (el argumento es el paso de muestreo):
 *   gcc -O2 -std=c11 -D_DEFAULT_SOURCE reproduction/float_geom_error.c -o /tmp/geof -lm
 *   /tmp/geof 4
 *
 * Copyright (c) 2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 * Licensed under the GNU General Public License v3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NX 21696
static const double SCALE = 1.4e-05, OFFX = -0.151865, OFFY = 0.151865;
static const double A = 6378137.0, B = 6356752.31414, HP = 35786023.0;
static const double LON0 = -75.0;
#define D2R (M_PI / 180.0)
#define R2D (180.0 / M_PI)

/* ---- navegacion (igual que navfloat.c) ---- */
static int nav_d(double x, double y, double *la, double *lo) {
    const double H = A + HP, a2 = A * A, b2 = B * B;
    double snx = sin(x), csx = cos(x), sny = sin(y), csy = cos(y);
    double a = snx * snx + csx * csx * (csy * csy + a2 / b2 * sny * sny);
    double b = -2.0 * H * csx * csy, disc = b * b - 4.0 * a * (H * H - a2);
    if (disc < 0) return 0;
    double rs = (-b - sqrt(disc)) / (2.0 * a);
    double px = rs * csx * csy, py = -rs * snx, pz = rs * csx * sny;
    *la = atan2(a2 * pz, b2 * sqrt((H - px) * (H - px) + py * py)) * R2D;
    *lo = (LON0 * D2R - atan2(py, H - px)) * R2D;
    return 1;
}
static int nav_f(float x, float y, float *la, float *lo) {
    const float H = (float)(A + HP), a2 = (float)(A * A), b2 = (float)(B * B);
    float snx = sinf(x), csx = cosf(x), sny = sinf(y), csy = cosf(y);
    float a = snx * snx + csx * csx * (csy * csy + a2 / b2 * sny * sny);
    float b = -2.0f * H * csx * csy, disc = b * b - 4.0f * a * (H * H - a2);
    if (disc < 0) return 0;
    float rs = (-b - sqrtf(disc)) / (2.0f * a);
    float px = rs * csx * csy, py = -rs * snx, pz = rs * csx * sny;
    *la = atan2f(a2 * pz, b2 * sqrtf((H - px) * (H - px) + py * py)) * (float)R2D;
    *lo = ((float)(LON0 * D2R) - atan2f(py, H - px)) * (float)R2D;
    return 1;
}

/* ---- efemeride: copia de solar_ephemeris(), siempre en double ---- */
typedef struct { double sd, cd, ha_base; } Eph;
static Eph ephem(int year, int month, int day, int hour, int min, int sec) {
    const double PI = M_PI, PI2 = 2 * M_PI;
    double UT = hour + min / 60.0 + sec / 3600.0;
    int yt, mt;
    if (month <= 2) { mt = month + 12; yt = year - 1; } else { mt = month; yt = year; }
    double t = (double)((int)(365.25 * (double)(yt - 2000)) + (int)(30.6001 * (double)(mt + 1)) -
                        (int)(0.01 * (double)(yt)) + day) + 0.0416667 * UT - 21958.0;
    double Dt = 96.4 + 0.00158 * t, te = t + 1.1574e-5 * Dt, wte = 0.0172019715 * te;
    double s1 = sin(wte), c1 = cos(wte), s2 = 2.0 * s1 * c1, c2 = (c1 + s1) * (c1 - s1);
    double s3 = s2 * c1 + c2 * s1, c3 = c2 * c1 - s2 * s1;
    double L = 1.7527901 + 1.7202792159e-2 * te + 3.33024e-2 * s1 - 2.0582e-3 * c1 + 3.512e-4 * s2 -
               4.07e-5 * c2 + 5.2e-6 * s3 - 9e-7 * c3 - 8.23e-5 * s1 * sin(2.92e-5 * te) +
               1.27e-5 * sin(1.49e-3 * te - 2.337) + 1.21e-5 * sin(4.31e-3 * te + 3.065) +
               2.33e-5 * sin(1.076e-2 * te - 1.533) + 3.49e-5 * sin(1.575e-2 * te - 2.358) +
               2.67e-5 * sin(2.152e-2 * te + 0.074) + 1.28e-5 * sin(3.152e-2 * te + 1.547) +
               3.14e-5 * sin(2.1277e-1 * te - 0.488);
    double nu = 9.282e-4 * te - 0.8, Dlam = 8.34e-5 * sin(nu), lambda = L + PI + Dlam;
    double epsi = 4.089567e-1 - 6.19e-9 * te + 4.46e-5 * cos(nu);
    double sl = sin(lambda), cl = cos(lambda), se = sin(epsi), ce = sqrt(1 - se * se);
    double RA = atan2(sl * ce, cl);
    if (RA < 0.0) RA += PI2;
    double Dec = asin(sl * se);
    Eph e = {sin(Dec), 0, 1.7528311 + 6.300388099 * t - RA + 0.92 * Dlam};
    e.cd = sqrt(1 - e.sd * e.sd);
    return e;
}

/* ---- geometria por pixel, parametrizada por tipo ---- */
#define SUN_BODY(T, SIN, COS, SQRT, ASIN, ATAN2, TAN, FMOD)                        \
    const T PI = (T)M_PI, PI2 = (T)(2 * M_PI), PIM = (T)M_PI_2;                    \
    T Lon = lo * PI / (T)180, Lat = la * PI / (T)180;                              \
    T HA = ha_base + Lon;                                                          \
    HA = FMOD(HA + PI, PI2) - PI;                                                  \
    if (HA < -PI) HA += PI2;                                                       \
    T sp = SIN(Lat), cp = SQRT((T)1 - sp * sp), sH = SIN(HA), cH = COS(HA);        \
    T se0 = sp * sd + cp * cd * cH;                                                \
    T ep = ASIN(se0) - (T)4.26e-5 * SQRT((T)1 - se0 * se0);                        \
    T Az = ATAN2(sH, cH * sp - sd * cp / cd);                                      \
    (void)TAN;                                                                     \
    *zen = (double)((PIM - ep) * (T)180 / PI);                                     \
    *azi = (double)(Az * (T)180 / PI);

static void sun_d(float la_, float lo_, double sd, double cd, double ha_base, double *zen, double *azi) {
    double la = la_, lo = lo_;
    SUN_BODY(double, sin, cos, sqrt, asin, atan2, tan, fmod)
}
static void sun_f(float la, float lo, float sd, float cd, float ha_base, double *zen, double *azi) {
    SUN_BODY(float, sinf, cosf, sqrtf, asinf, atan2f, tanf, fmodf)
}

#define SAT_BODY(T, SIN, COS, SQRT, ACOS, ATAN2, FMAX, FMIN)                       \
    const T a = (T)6378137.0, f = (T)(1.0 / 298.257223563);                        \
    T lat = pla * (T)M_PI / (T)180, lon = plo * (T)M_PI / (T)180;                  \
    T slon = (T)sat_lon * (T)M_PI / (T)180;                                        \
    T N = a / SQRT((T)1 - ((T)2 * f - f * f) * SIN(lat) * SIN(lat));               \
    T xp = N * COS(lat) * COS(lon), yp = N * COS(lat) * SIN(lon);                  \
    T zp = N * ((T)1 - ((T)2 * f - f * f)) * SIN(lat);                             \
    T sr = a + (T)sat_h, xs = sr * COS(slon), ys = sr * SIN(slon);                 \
    T dx = xp - xs, dy = yp - ys, dz = zp;                                         \
    T dist = SQRT(dx * dx + dy * dy + dz * dz);                                    \
    dx /= dist; dy /= dist; dz /= dist;                                            \
    T nl = SQRT(xp * xp + yp * yp + zp * zp);                                      \
    T cv = -(dx * xp / nl + dy * yp / nl + dz * zp / nl);                          \
    *vza = (double)(ACOS(FMAX((T)-1, FMIN((T)1, cv))) * (T)180 / (T)M_PI);         \
    T ve = dx * -SIN(lon) + dy * COS(lon);                                         \
    T vn = dx * -SIN(lat) * COS(lon) + dy * -SIN(lat) * SIN(lon) + dz * COS(lat);  \
    *vaa = (double)(ATAN2(ve, vn) * (T)180 / (T)M_PI);

static void sat_d(float pla_, float plo_, float sat_lon, float sat_h, double *vza, double *vaa) {
    double pla = pla_, plo = plo_;
    SAT_BODY(double, sin, cos, sqrt, acos, atan2, fmax, fmin)
}
static void sat_f(float pla, float plo, float sat_lon, float sat_h, double *vza, double *vaa) {
    SAT_BODY(float, sinf, cosf, sqrtf, acosf, atan2f, fmaxf, fminf)
}

/* ganancia de apply_solar_zenith_correction(), evaluada siempre en double para
 * aislar el efecto del angulo */
static double gain(double sza) {
    if (sza >= 95.0) return 0;
    if (sza < 88.0) return 1.0 / cos(sza * D2R);
    double fade = 1.0 - log1p((sza - 88.0) / 7.0) / log(2.0);
    return (fade < 0 ? 0 : fade) / cos(88.0 * D2R);
}

static double angdiff(double a, double b) {
    double d = fmod(fabs(a - b), 360.0);
    return d > 180 ? 360 - d : d;
}

/* acumuladores: max y p99 aproximado por histograma logaritmico */
#define NB 400
typedef struct { double max, sum; size_t n; size_t h[NB]; } Acc;
static void acc(Acc *a, double v) {
    if (v > a->max) a->max = v;
    a->sum += v; a->n++;
    int b = v <= 1e-12 ? 0 : (int)((log10(v) + 12) * 20); /* 1e-12 .. 1e8 */
    if (b < 0) b = 0;
    if (b >= NB) b = NB - 1;
    a->h[b]++;
}
static double pct(const Acc *a, double q) {
    size_t tgt = (size_t)(q * a->n), c = 0;
    for (int b = 0; b < NB; b++) { c += a->h[b]; if (c > tgt) return pow(10, (b + 1) / 20.0 - 12); }
    return a->max;
}
static void pr(const char *name, const Acc *a) {
    if (!a->n) { printf("  %-38s (sin muestras)\n", name); return; }
    printf("  %-38s %9zu  media %9.3g  p99<= %9.3g  max %9.3g\n", name, a->n, a->sum / a->n,
           pct(a, 0.99), a->max);
}

enum { BAND_DAY, BAND_TW, BAND_FADE, NBAND };
static const char *bandname[NBAND] = {"SZA < 80", "80 <= SZA < 88", "88 <= SZA < 95"};
enum { M_SZA, M_SAA, M_VZA, M_VAA, M_GAIN_DN, M_SECV_DN, NM };
static const char *mname[NM] = {"|dSZA| (grados)", "|dSAA| (grados)", "|dVZA| (grados)",
                                "|dVAA| (grados)", "ganancia cenit solar (DN/255)",
                                "sec(VZA) relativo (DN/255)"};
enum { V_G, V_NG, V_GN, NVAR };
static const char *vname[NVAR] = {
    "G: geometria float (misma lat/lon float, ha_base reducida)",
    "NG: navegacion float + geometria float",
    "Gn: geometria float con ha_base SIN reducir (float ingenuo)"};

int main(int argc, char **argv) {
    int stride = argc > 1 ? atoi(argv[1]) : 4;
    /* escena del articulo: GOES-19 FD 2026-08-11 18:00:21 UTC */
    Eph e = ephem(2026, 8, 11, 18, 0, 21);
    double hb_red = fmod(e.ha_base, 2 * M_PI);
    const float sat_lon = -75.0f, sat_h = 35786023.0f;
    printf("ha_base = %.6f rad; reducida mod 2pi = %.6f; float(ha_base) - ha_base = %.3g rad\n\n",
           e.ha_base, hb_red, (double)(float)e.ha_base - e.ha_base);

    static Acc st[NVAR][NBAND][NM];
    for (int j = 0; j < NX; j += stride)
        for (int i = 0; i < NX; i += stride) {
            double x = OFFX + i * SCALE, y = OFFY - j * SCALE, lad, lod;
            float laf, lof;
            if (!nav_d(x, y, &lad, &lod) || !nav_f((float)x, (float)y, &laf, &lof)) continue;
            float la = (float)lad, lo = (float)lod; /* lo que guarda DataF */

            double z0, a0, v0, w0;
            sun_d(la, lo, e.sd, e.cd, e.ha_base, &z0, &a0);
            sat_d(la, lo, sat_lon, sat_h, &v0, &w0);
            if (z0 >= 95.0) continue; /* noche: ganancia cero, nada que comparar */
            int bd = z0 < 80 ? BAND_DAY : z0 < 88 ? BAND_TW : BAND_FADE;

            double z[NVAR], az[NVAR], v[NVAR], w[NVAR];
            sun_f(la, lo, (float)e.sd, (float)e.cd, (float)hb_red, &z[V_G], &az[V_G]);
            sat_f(la, lo, sat_lon, sat_h, &v[V_G], &w[V_G]);
            sun_f(laf, lof, (float)e.sd, (float)e.cd, (float)hb_red, &z[V_NG], &az[V_NG]);
            sat_f(laf, lof, sat_lon, sat_h, &v[V_NG], &w[V_NG]);
            sun_f(la, lo, (float)e.sd, (float)e.cd, (float)e.ha_base, &z[V_GN], &az[V_GN]);
            sat_f(la, lo, sat_lon, sat_h, &v[V_GN], &w[V_GN]);

            double g0 = gain(z0), s0 = 1.0 / cos(v0 * D2R);
            for (int k = 0; k < NVAR; k++) {
                Acc *s = st[k][bd];
                acc(&s[M_SZA], fabs(z[k] - z0));
                acc(&s[M_SAA], angdiff(az[k], a0));
                acc(&s[M_VZA], fabs(v[k] - v0));
                acc(&s[M_VAA], angdiff(w[k], w0));
                /* cota en DN lineales: salida = rho*g con rho <= 1 y salida
                 * <= 1 (escala completa), asi que el peor rho es min(1, 1/g0)
                 * y el error es 255*|dg|*rho. Evita dividir por g0 -> 0 cerca
                 * de 95 grados, donde el error relativo no se ve */
                double rho = g0 > 1 ? 1.0 / g0 : 1.0;
                acc(&s[M_GAIN_DN], 255.0 * fabs(gain(z[k]) - g0) * rho);
                if (v0 < 89)
                    acc(&s[M_SECV_DN], 255.0 * fabs(1.0 / cos(v[k] * D2R) - s0) / s0);
            }
        }

    printf("GOES-19 FD 0.5 km, paso %d; referencia = produccion (nav double->float, geometria double)\n", stride);
    for (int k = 0; k < NVAR; k++) {
        printf("\n== %s ==\n", vname[k]);
        for (int b = 0; b < NBAND; b++) {
            printf(" %s\n", bandname[b]);
            for (int m = 0; m < NM; m++) pr(mname[m], &st[k][b][m]);
        }
    }
    return 0;
}
