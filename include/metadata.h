#ifndef METADATA_H
#define METADATA_H

#include <stdbool.h>

// Handle opaco
typedef struct MetadataContext MetadataContext;

// Ciclo de vida
MetadataContext* metadata_create(void);
void metadata_destroy(MetadataContext *ctx);

// Setters específicos (Puentes para tu código actual)
void metadata_set_satellite(MetadataContext *ctx, const char *sat);
void metadata_set_geometry(MetadataContext *ctx, float lon_min, float lat_max, float lon_max, float lat_min);
void metadata_set_time(MetadataContext *ctx, const char *iso_time);

// API Genérica (C17) para agregar estadísticas
void metadata_add_dbl(MetadataContext *ctx, const char *key, double val);
void metadata_add_int(MetadataContext *ctx, const char *key, int val);
void metadata_add_str(MetadataContext *ctx, const char *key, const char *val);
void metadata_add_bool(MetadataContext *ctx, const char *key, bool val);

#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        int:         metadata_add_int,    \
        double:      metadata_add_dbl,    \
        float:       metadata_add_dbl,    \
        char*:       metadata_add_str,    \
        const char*: metadata_add_str,    \
        bool:        metadata_add_bool    \
    )(CTX, KEY, VAL)

// Guardar
int metadata_save_json(MetadataContext *ctx, const char *filename);

#endif
