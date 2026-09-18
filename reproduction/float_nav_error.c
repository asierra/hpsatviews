/* Error de geolocalizacion en float frente a double, disco completo ABI.
 *
 * Directo: la forma cerrada de compute_navigation_nc() (src/reader_nc.c),
 * evaluada una vez en double (referencia) y otra con toda la aritmetica en
 * float. Error = distancia geodesica entre ambas (m), y en pixeles locales
 * (distancia al vecino en x, calculada en double).
 *
 * Inverso: la forma de src/reprojection.c llevando la lat/lon de referencia de
 * cada pixel a (col,row). En double debe devolver (i,j); en float, el desvio es
 * el error en pixeles de fuente.
 *
 * Tambien: el "baseline" de produccion, double calculado y guardado en float
 * (DataF), para separar la cuantizacion del almacenamiento del error de calculo.
 *
 * La malla es la de GOES-19 a 0.5 km (21696 x 21696, lon_0 = -75). No lee
 * ningun archivo: los parametros del grid estan fijos abajo.
 *
 * Compilar y ejecutar (el argumento es el paso de muestreo, 4 = uno de 4x4):
 *   gcc -O2 -std=c11 -D_DEFAULT_SOURCE reproduction/float_nav_error.c -o /tmp/navf -lm
 *   /tmp/navf 4
 *
 * Copyright (c) 2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 * Licensed under the GNU General Public License v3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NX 21696
static const double SCALE = 1.4e-05, OFFX = -0.151865, OFFY = 0.151865;
static const double A = 6378137.0, B = 6356752.31414, HP = 35786023.0;
static const double LON0 = -75.0;
#define D2R (M_PI / 180.0)
#define R2D (180.0 / M_PI)

static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* distancia geodesica aproximada (haversine sobre esfera de radio A); para
 * errores de metros la diferencia con el elipsoide es despreciable */
static double dist_m(double la1, double lo1, double la2, double lo2) {
    double p1 = la1 * D2R, p2 = la2 * D2R, dp = p2 - p1, dl = (lo2 - lo1) * D2R;
    double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2 * A * asin(sqrt(h));
}

static int nav_d(double x, double y, double *la, double *lo) {
    const double H = A + HP, a2 = A * A, b2 = B * B;
    double snx = sin(x), csx = cos(x), sny = sin(y), csy = cos(y);
    double a = snx * snx + csx * csx * (csy * csy + a2 / b2 * sny * sny);
    double b = -2.0 * H * csx * csy;
    double disc = b * b - 4.0 * a * (H * H - a2);
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
    float b = -2.0f * H * csx * csy;
    float disc = b * b - 4.0f * a * (H * H - a2);
    if (disc < 0) return 0;
    float rs = (-b - sqrtf(disc)) / (2.0f * a);
    float px = rs * csx * csy, py = -rs * snx, pz = rs * csx * sny;
    *la = atan2f(a2 * pz, b2 * sqrtf((H - px) * (H - px) + py * py)) * (float)R2D;
    *lo = ((float)(LON0 * D2R) - atan2f(py, H - px)) * (float)R2D;
    return 1;
}

/* inverso de src/reprojection.c; devuelve 0 si no es visible */
#define INV_BODY(T, SIN, COS, TAN, ATAN, SQRT, ASIN, ATAN2)                     \
    const T H = (T)(A + HP), a2 = (T)(A * A), b2 = (T)(B * B), b = (T)B;       \
    const T e2 = (T)1 - b2 / a2;                                               \
    T phi = (T)la * (T)D2R, lam = (T)lo * (T)D2R;                              \
    T phi_c = ATAN((b2 / a2) * TAN(phi));                                      \
    T cpc = COS(phi_c), spc = SIN(phi_c);                                      \
    T r_c = b / SQRT((T)1 - e2 * cpc * cpc);                                   \
    T dl = lam - (T)(LON0 * D2R);                                              \
    T sx = H - r_c * cpc * COS(dl), sy = -r_c * cpc * SIN(dl), sz = r_c * spc; \
    if (H * (H - sx) < sy * sy + (a2 / b2) * sz * sz) return 0;                \
    T sn = SQRT(sx * sx + sy * sy + sz * sz);                                  \
    T xr = ASIN(-sy / sn), yr = ATAN2(sz, sx);                                 \
    *col = (double)((xr - (T)OFFX) / (T)SCALE);                                \
    *row = (double)((yr - (T)OFFY) / (T)(-SCALE));                             \
    return 1;

static int inv_d(double la, double lo, double *col, double *row) {
    INV_BODY(double, sin, cos, tan, atan, sqrt, asin, atan2)
}
static int inv_f(double la, double lo, double *col, double *row) {
    INV_BODY(float, sinf, cosf, tanf, atanf, sqrtf, asinf, atan2f)
}

/* cenit del satelite en el punto (grados), para separar el limbo */
static double satzen(double la, double lo) {
    double p = la * D2R, dl = (lo - LON0) * D2R;
    double e2 = 1 - B * B / (A * A), N = A / sqrt(1 - e2 * sin(p) * sin(p));
    double g[3] = {N * cos(p) * cos(dl), N * cos(p) * sin(dl), N * (1 - e2) * sin(p)};
    double n[3] = {cos(p) * cos(dl), cos(p) * sin(dl), sin(p)};
    double v[3] = {A + HP - g[0], -g[1], -g[2]};
    double vn = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return acos((n[0] * v[0] + n[1] * v[1] + n[2] * v[2]) / vn) * R2D;
}

typedef struct { float *v; size_t n; } Vec;
static void report(const char *name, const char *unit, Vec *v) {
    if (!v->n) { printf("%-44s  (sin muestras)\n", name); return; }
    qsort(v->v, v->n, sizeof(float), cmpf);
    double s = 0; for (size_t k = 0; k < v->n; k++) s += v->v[k];
    #define Q(q) v->v[(size_t)((q) * (v->n - 1))]
    printf("%-44s %9zu  media %9.3g  p50 %9.3g  p95 %9.3g  p99 %9.3g  max %9.3g %s\n",
           name, v->n, s / v->n, Q(0.5), Q(0.95), Q(0.99), v->v[v->n - 1], unit);
}

int main(int argc, char **argv) {
    int stride = argc > 1 ? atoi(argv[1]) : 4;
    size_t cap = (size_t)(NX / stride + 1) * (NX / stride + 1);
    enum { F_M, F_PX, S_M, I_PX, F_M80, F_PX80, I_PX80, S_M80, NV };
    const char *nm[NV] = {
        "directo float, disco completo [m]", "directo float, disco completo [px]",
        "double guardado en float (produccion) [m]", "inverso float, disco completo [px]",
        "directo float, cenit sat < 80 [m]", "directo float, cenit sat < 80 [px]",
        "inverso float, cenit sat < 80 [px]", "double guardado en float, < 80 [m]"};
    Vec v[NV];
    for (int k = 0; k < NV; k++) { v[k].v = malloc(cap * sizeof(float)); v[k].n = 0; }
    size_t nodisc_f = 0, inv_d_worst_i = 0; double inv_d_worst = 0;

    for (int j = 0; j < NX; j += stride) {
        for (int i = 0; i + 1 < NX; i += stride) {
            double x = OFFX + i * SCALE, y = OFFY - j * SCALE;
            double la, lo, la1, lo1;
            if (!nav_d(x, y, &la, &lo) || !nav_d(x + SCALE, y, &la1, &lo1)) continue;
            double gsd = dist_m(la, lo, la1, lo1);
            int core = satzen(la, lo) < 80.0;

            float laf, lof;
            if (!nav_f((float)x, (float)y, &laf, &lof)) { nodisc_f++; continue; }
            double e = dist_m(la, lo, laf, lof);
            double q = dist_m(la, lo, (float)la, (float)lo);
            v[F_M].v[v[F_M].n++] = e;  v[F_PX].v[v[F_PX].n++] = e / gsd;
            v[S_M].v[v[S_M].n++] = q;
            if (core) { v[F_M80].v[v[F_M80].n++] = e; v[F_PX80].v[v[F_PX80].n++] = e / gsd;
                        v[S_M80].v[v[S_M80].n++] = q; }

            double cd, rd, cf, rf;
            if (inv_d(la, lo, &cd, &rd)) {
                double ed = hypot(cd - i, rd - j);
                if (ed > inv_d_worst) { inv_d_worst = ed; inv_d_worst_i = i; }
                if (inv_f(la, lo, &cf, &rf)) {
                    double ef = hypot(cf - cd, rf - rd);
                    v[I_PX].v[v[I_PX].n++] = ef;
                    if (core) v[I_PX80].v[v[I_PX80].n++] = ef;
                }
            }
        }
    }
    printf("GOES-19 disco completo 0.5 km (%dx%d), paso %d\n", NX, NX, stride);
    printf("pixeles que float saca del disco (double los ve): %zu\n", nodisc_f);
    printf("control: inverso double vs (i,j), peor desvio %.3g px\n\n", inv_d_worst);
    (void)inv_d_worst_i;
    int order[NV] = {S_M, S_M80, F_M, F_M80, F_PX, F_PX80, I_PX, I_PX80};
    for (int k = 0; k < NV; k++) report(nm[order[k]], "", &v[order[k]]);
    return 0;
}
