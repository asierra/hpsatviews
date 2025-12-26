#ifndef HPSATVIEWS_VERSION_H
#define HPSATVIEWS_VERSION_H

// --- Definición de la Versión (Single Source of Truth) ---
#define HPSV_VERSION_MAJOR 1
#define HPSV_VERSION_MINOR 0
#define HPSV_VERSION_PATCH 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Construcción automática: "1.0.0"
#define HPSV_VERSION_STRING \
    "High Performance Satellite Views (hpsv) " STR(HPSV_VERSION_MAJOR) "." STR(HPSV_VERSION_MINOR) "." STR(HPSV_VERSION_PATCH)

#endif // HPSATVIEWS_VERSION_H
