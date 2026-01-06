#ifndef HPSATVIEWS_METADATA_H_
#define HPSATVIEWS_METADATA_H_

#include <stdbool.h>
#include "datanc.h" 

/**
 * Handle opaco para el contexto de metadatos.
 * Oculta la implementación (probablemente cJSON o buffer manual) al usuario.
 */
typedef struct MetadataContext MetadataContext;

// --- Ciclo de Vida ---

/**
 * Crea un nuevo contexto de metadatos vacío.
 */
MetadataContext* metadata_create(void);

/**
 * Libera la memoria asociada al contexto.
 */
void metadata_destroy(MetadataContext *ctx);

// --- API de Agregación de Datos (Fluent / Polymorphic) ---

void metadata_add_int(MetadataContext *ctx, const char *key, int value);
void metadata_add_dbl(MetadataContext *ctx, const char *key, double value);
void metadata_add_str(MetadataContext *ctx, const char *key, const char *value);
void metadata_add_bool(MetadataContext *ctx, const char *key, bool value);

/**
 * Macro C11 _Generic para inserción polimórfica de metadatos.
 * Uso: metadata_add(ctx, "gamma", 1.5);
 */
#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        bool:         metadata_add_bool, \
        int:          metadata_add_int, \
        float:        metadata_add_dbl, \
        double:       metadata_add_dbl, \
        char*:        metadata_add_str, \
        const char*:  metadata_add_str \
    )(CTX, KEY, VAL)

// --- Funciones Específicas de Dominio ---

/**
 * Establece el comando/modo de procesamiento.
 */
void metadata_set_command(MetadataContext *ctx, const char *command);

/**
 * Registra la geometría final de la imagen.
 * @param x1 lon_min
 * @param y1 lat_max
 * @param x2 lon_max
 * @param y2 lat_min
 */
void metadata_set_geometry(MetadataContext *ctx, float x1, float y1, float x2, float y2);

/**
 * Registra información inicial de un canal o componente.
 * @param nc Estructura con metadatos que se obtienen al leer un NetCDF
 */
void metadata_from_nc(MetadataContext *ctx, const DataNC *nc);

// --- Generación de Salidas ---

/**
 * Genera el nombre de archivo estandarizado basado en los metadatos acumulados.
 * Reemplaza la lógica antigua de filename_utils.
 * 
 * @param ctx Contexto con datos del satélite, fecha y producto.
 * @param extension Extensión deseada (ej. ".png", ".json").
 * @return String alojado dinámicamente (caller debe liberar) o NULL en error.
 */
char* metadata_build_filename(const MetadataContext *ctx, const char *extension);

/**
 * Serializa los metadatos a un archivo JSON en disco.
 * 
 * @param ctx Contexto a serializar.
 * @param filename Ruta de salida.
 * @return 0 en éxito, no-cero en error.
 */
int metadata_save_json(MetadataContext *ctx, const char *filename);

#endif // HPSATVIEWS_METADATA_H_
