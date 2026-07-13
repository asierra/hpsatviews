/* Version string definitions (single source of truth).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_VERSION_H_
#define HPSATVIEWS_VERSION_H_

// Version components (major.minor.patch).
#define HPSV_VERSION_MAJOR 1
#define HPSV_VERSION_MINOR 1
#define HPSV_VERSION_PATCH 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Bare version number, e.g. "1.0.0".
#define HPSV_VERSION \
    STR(HPSV_VERSION_MAJOR) "." STR(HPSV_VERSION_MINOR) "." STR(HPSV_VERSION_PATCH)

// Full version banner string, e.g. "High Performance Satellite Views (hpsv) 1.0.0".
#define HPSV_VERSION_STRING \
    "High Performance Satellite Views (hpsv) " HPSV_VERSION

#endif /* HPSATVIEWS_VERSION_H_ */
