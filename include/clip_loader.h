/* Geographic clip region loader from a CSV configuration file.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_CLIP_LOADER_H_
#define HPSATVIEWS_CLIP_LOADER_H_

// Geographic bounding box entry loaded from a clip config file.
typedef struct {
    char clave[32];   // clip key (e.g., "MEX", "CAM")
    char region[64];  // human-readable region name
    double ul_x;      // upper-left longitude (degrees east)
    double ul_y;      // upper-left latitude (degrees north)
    double lr_x;      // lower-right longitude (degrees east)
    double lr_y;      // lower-right latitude (degrees north)
    int encontrado;   // 1 if found, 0 otherwise
} GeoClip;

// Returns the GeoClip entry for the given key from a CSV config file.
GeoClip buscar_clip_por_clave(const char *ruta_archivo, const char *clave_buscada);

// Lists all available clip keys to stdout.
void listar_clips_disponibles(const char *ruta_archivo);

#endif /* HPSATVIEWS_CLIP_LOADER_H_ */