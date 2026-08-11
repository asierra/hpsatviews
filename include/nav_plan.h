/* Parámetros de la proyección de rejilla fija (geos) necesarios para convertir
 * ángulos de escaneo (x, y) a lat/lon, extraídos una sola vez del NetCDF.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Fuente única para las rutas CPU y CUDA, igual que reproject_build_plan() hace
 * con la reproyección: el setup (abrir el archivo, leer los atributos de
 * goes_imager_projection y los arreglos x[]/y[]) se hace una vez y ambos
 * consumidores parten exactamente de los mismos números. Así la equivalencia
 * CPU/GPU no depende de mantener dos lecturas sincronizadas a mano.
 */
#ifndef HPSATVIEWS_NAV_PLAN_H_
#define HPSATVIEWS_NAV_PLAN_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t width, height;   /* tamaño de la rejilla del archivo */
    double H;               /* semieje mayor + altura del satélite (m) */
    double lambda_0;        /* longitud del origen de proyección (rad) */
    double sm_maj, sm_min;  /* semiejes del elipsoide (m) */
    /* Ángulos de escaneo ya desempacados (rad): x_rad[width], y_rad[height].
     * Los posee el NavPlan; liberar con nav_plan_destroy(). */
    double *x_rad;
    double *y_rad;
} NavPlan;

/* Llena el plan desde un archivo GOES L1b/L2. Devuelve 0 en éxito; ante fallo
 * deja el plan en cero y no hay nada que liberar. */
int nav_build_plan(const char *filename, NavPlan *plan);

/* Libera los arreglos del plan y lo deja en cero. Seguro con plan==NULL. */
void nav_plan_destroy(NavPlan *plan);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_NAV_PLAN_H_ */
