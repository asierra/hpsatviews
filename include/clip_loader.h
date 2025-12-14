#ifndef CLIP_LOADER_H
#define CLIP_LOADER_H

// Estructura para almacenar los límites del recorte geográfico
typedef struct {
    char clave[32];
    char region[64];
    double ul_x; // Upper Left X (Longitude)
    double ul_y; // Upper Left Y (Latitude)
    double lr_x; // Lower Right X (Longitude)
    double lr_y; // Lower Right Y (Latitude)
    int encontrado; // Flag: 1 si existe, 0 si no
} GeoClip; // Renombrado de GeoRecorte a GeoClip para consistencia


GeoClip buscar_clip_por_clave(const char *ruta_archivo, const char *clave_buscada);

void listar_clips_disponibles(const char *ruta_archivo);

#endif