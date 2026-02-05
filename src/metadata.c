/*
 * Módulo de Metadatos - Gestión y serialización
 * Sprint 1-2: Implementación base + JSON
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "metadata.h"
#include "writer_json.h"
#include "datanc.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define MAX_KV 32
#define MAX_CHANNELS 16

typedef struct {
    char key[32];
    char val_s[64];
    double val_d;
    int type; // 0=dbl, 1=str, 2=int, 3=bool
} KeyVal;

typedef struct {
    char name[16];          // "C13", "Red", etc.
    char quantity[32];      // "brightness_temperature", "reflectance"
    double min;
    double max;
    char unit[16];          // "K", "percent", etc.
    bool valid;
} ChannelInfo;

struct MetadataContext {
    char tool[32];
    char version[16];
    char command[32];
    const char *satellite;
    char time_iso[32];
    time_t timestamp;
    
    float bbox[4];
    char projection[32];
    bool has_bbox;
    
    ChannelInfo channels[MAX_CHANNELS];
    int channel_count;
    
    // Almacenamiento temporal simple para campos extra
    KeyVal extra_fields[MAX_KV];
    int count;
};

static const char *SAT_NAMES[] = {
    [SAT_UNKNOWN] = "unknown",
    [SAT_GOES16]  = "G16",
    [SAT_GOES17]  = "G17",
    [SAT_GOES18]  = "G18",
    [SAT_GOES19]  = "G19"
};

static const char* get_sat_name(SatelliteID id) {
    if (id >= SAT_UNKNOWN && id <= SAT_GOES19) {
        return SAT_NAMES[id];
    }
    return "unknown";
}

MetadataContext* metadata_create(void) {
    MetadataContext *ctx = calloc(1, sizeof(MetadataContext));
    if (ctx) {
        strncpy(ctx->tool, "hpsatviews", sizeof(ctx->tool) - 1);
        strncpy(ctx->projection, "geographics", sizeof(ctx->projection) - 1);
    }
    return ctx;
}

void metadata_destroy(MetadataContext *ctx) {
    free(ctx);
}

void metadata_from_nc(MetadataContext *ctx, const DataNC *nc) {
    if (!ctx || !nc) return;	
    // 1. Copiar Timestamp y convertir a ISO 8601
    ctx->timestamp = nc->timestamp;
    if (nc->timestamp > 0) {
        struct tm tm_info;
        gmtime_r(&nc->timestamp, &tm_info);
        strftime(ctx->time_iso, sizeof(ctx->time_iso), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    }

    // 2. Copiar Satélite
	ctx->satellite = get_sat_name(nc->sat_id);
	LOG_DEBUG("Satélite ID %d nombre %s", nc->sat_id, ctx->satellite);

    // 3. Agregar información del canal
    if (ctx->channel_count < MAX_CHANNELS && nc->varname) {
        ChannelInfo *ch = &ctx->channels[ctx->channel_count];
        strncpy(ch->name, nc->varname, sizeof(ch->name) - 1);
        
        // Determinar quantity basado en el tipo de datos
        if (nc->is_float) {
            ch->min = nc->fdata.fmin;
            ch->max = nc->fdata.fmax;
            // TODO: Detectar si es temperatura o reflectancia basado en el canal
            strncpy(ch->quantity, "raw_data", sizeof(ch->quantity) - 1);
            strncpy(ch->unit, "", sizeof(ch->unit) - 1);
        } else {
            ch->min = nc->bdata.min;
            ch->max = nc->bdata.max;
            strncpy(ch->quantity, "raw_counts", sizeof(ch->quantity) - 1);
            strncpy(ch->unit, "", sizeof(ch->unit) - 1);
        }
        ch->valid = true;
        ctx->channel_count++;
    }
}

void metadata_set_command(MetadataContext *ctx, const char *command) {
    if (!ctx || !command) return;
    strncpy(ctx->command, command, sizeof(ctx->command) - 1);
}

void metadata_set_projection(MetadataContext *ctx, const char *proj) {
    if (!ctx || !proj) return;
    strncpy(ctx->projection, proj, sizeof(ctx->projection) - 1);
}

void metadata_set_geometry(MetadataContext *ctx, float x1, float y1, float x2, float y2) {
    if(!ctx) return;
    ctx->bbox[0] = x1; ctx->bbox[1] = y1;
    ctx->bbox[2] = x2; ctx->bbox[3] = y2;
    ctx->has_bbox = true;
}

// Implementación de los adders
void metadata_add_dbl(MetadataContext *c, const char *k, double v) {
    if(c->count >= MAX_KV) return;
    strncpy(c->extra_fields[c->count].key, k, 31);
    c->extra_fields[c->count].val_d = v;
    c->extra_fields[c->count].type = 0;
    c->count++;
}
void metadata_add_str(MetadataContext *c, const char *k, const char *v) {
    if(c->count >= MAX_KV) return;
    strncpy(c->extra_fields[c->count].key, k, 31);
    strncpy(c->extra_fields[c->count].val_s, v, 63);
    c->extra_fields[c->count].type = 1;
    c->count++;
}
void metadata_add_int(MetadataContext *c, const char *k, int v) {
    metadata_add_dbl(c, k, (double)v); // Simplificación
    c->extra_fields[c->count-1].type = 2;
}
void metadata_add_bool(MetadataContext *c, const char *k, bool v) {
    metadata_add_dbl(c, k, v ? 1.0 : 0.0);
    c->extra_fields[c->count-1].type = 3;
}

/**
 * Formatea timestamp en formato YYYYJJJ_hhmm (año juliano).
 * Absorbe funcionalidad de filename_utils.c
 */
static void format_timestamp_julian(time_t timestamp, char* buffer, size_t size) {
    if (timestamp == 0) {
        snprintf(buffer, size, "NA");
        return;
    }
    struct tm tm_info;
    gmtime_r(&timestamp, &tm_info);
    strftime(buffer, size, "%Y%j_%H%M", &tm_info);
}

/**
 * Construye la cadena de operaciones aplicadas (ej: "clahe__geo__inv").
 * Retorna true si se agregó alguna operación.
 */
static bool build_ops_string(const MetadataContext *ctx, char* buffer, size_t size) {
    buffer[0] = '\0';
    
    const char* ops_list[10];
    int op_count = 0;
    char gamma_str[16];
    
    // Buscar las operaciones en extra_fields
    bool has_gamma = false, has_clahe = false, has_histo = false;
    bool has_rayleigh = false, has_invert = false;
    float gamma_val = 1.0f;
    
    for (int i = 0; i < ctx->count; i++) {
        const KeyVal *kv = &ctx->extra_fields[i];
        if (strcmp(kv->key, "gamma") == 0 && kv->type == 0) {
            gamma_val = (float)kv->val_d;
            if (fabsf(gamma_val - 1.0f) > 0.01f) has_gamma = true;
        } else if (strcmp(kv->key, "clahe") == 0 && kv->type == 3) {
            has_clahe = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "histogram") == 0 && kv->type == 3) {
            has_histo = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "rayleigh") == 0 && kv->type == 3) {
            has_rayleigh = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "invert") == 0 && kv->type == 3) {
            has_invert = (kv->val_d != 0.0);
        }
    }
    
    // Construir lista de operaciones en orden
    if (has_invert) ops_list[op_count++] = "inv";
    if (has_rayleigh) ops_list[op_count++] = "ray";
    if (has_histo) ops_list[op_count++] = "histo";
    if (has_clahe) ops_list[op_count++] = "clahe";
    if (has_gamma) {
        snprintf(gamma_str, sizeof(gamma_str), "g%.1f", gamma_val);
        // Reemplazar punto por 'p' (ej: "g1.5" -> "g1p5")
        for (char *p = gamma_str; *p; ++p) {
            if (*p == '.') *p = 'p';
        }
        ops_list[op_count++] = gamma_str;
    }
    if (ctx->has_bbox) ops_list[op_count++] = "clip";
    
    // Buscar "reprojection" o "geographics" en extra_fields
    for (int i = 0; i < ctx->count; i++) {
        if ((strcmp(ctx->extra_fields[i].key, "reprojection") == 0 ||
             strcmp(ctx->extra_fields[i].key, "geographics") == 0) &&
            ctx->extra_fields[i].type == 3 && ctx->extra_fields[i].val_d != 0.0) {
            ops_list[op_count++] = "geo";
            break;
        }
    }
    
    if (op_count == 0) {
        return false;
    }
    
    // Construir la cadena final separada por "__"
    size_t current_len = 0;
    for (int i = 0; i < op_count; i++) {
        size_t op_len = strlen(ops_list[i]);
        if (current_len + op_len + (i > 0 ? 2 : 0) + 1 < size) {
            if (i > 0) {
                strcat(buffer, "__");
                current_len += 2;
            }
            strcat(buffer, ops_list[i]);
            current_len += op_len;
        }
    }
    return (op_count > 0);
}

char* metadata_build_filename(const MetadataContext *ctx, const char *extension) {
    if (!ctx || !extension) return NULL;
    
    // Formato: hpsv_<SAT>_<YYYYJJJ_hhmm>_<TIPO>_<BANDAS>[_<OPS>].<ext>
    char *filename = malloc(512);
    if (!filename) return NULL;
    
    // 1. Satélite
    const char *sat = ctx->satellite[0] ? ctx->satellite : "GXX";
    
    // 2. Timestamp (formato juliano)
    char instant[20];
    format_timestamp_julian(ctx->timestamp, instant, sizeof(instant));
    
    // 3. Tipo de producto (basado en command)
    const char *type = "output";
    if (ctx->command[0]) {
        if (strcmp(ctx->command, "gray") == 0) type = "gray";
        else if (strcmp(ctx->command, "pseudocolor") == 0) type = "pseudo";
        else if (strcmp(ctx->command, "rgb") == 0) type = "rgb";
        else type = ctx->command;
    }
    
    // 4. Bandas (simplificado: usar el primer canal)
    char bands[32] = "NA";
    if (ctx->channel_count > 0 && ctx->channels[0].valid) {
        strncpy(bands, ctx->channels[0].name, sizeof(bands) - 1);
    }
    
    // 5. Operaciones aplicadas
    char ops[128] = "";
    bool has_ops = build_ops_string(ctx, ops, sizeof(ops));
    
    // 6. Construir nombre final
    if (has_ops) {
        snprintf(filename, 512, "hpsv_%s_%s_%s_%s_%s%s",
                 sat, instant, type, bands, ops, extension);
    } else {
        snprintf(filename, 512, "hpsv_%s_%s_%s_%s%s",
                 sat, instant, type, bands, extension);
    }
    
    return filename;
}

int metadata_save_json(MetadataContext *ctx, const char *filename) {
    JsonWriter *w = json_create(filename);
    if (!w) return -1;

    // Campos requeridos del schema
    json_write(w, "tool", ctx->tool[0] ? ctx->tool : "hpsatviews");
    json_write(w, "version", "1.0");  // TODO: usar HPSV_VERSION_STRING
    if (ctx->command[0]) json_write(w, "command", ctx->command);
    if (ctx->satellite) json_write(w, "satellite", ctx->satellite);
    if (ctx->time_iso[0]) json_write(w, "timestamp", ctx->time_iso);

    // Campos para mapdrawer (CRS y Bounds en raíz)
    if (ctx->projection[0]) {
        json_write_string(w, "crs", ctx->projection);
    }
    if (ctx->has_bbox) {
        json_write_float_array(w, "bounds", ctx->bbox, 4);
    }

    // Geometría
    if (ctx->has_bbox) {
        json_begin_object(w, "geometry");
        json_write(w, "projection", ctx->projection);
        json_write_float_array(w, "bbox", ctx->bbox, 4);
        json_end_object(w);
    }

    // Canales (array de objetos)
    if (ctx->channel_count > 0) {
        json_begin_array(w, "channels");
        for (int i = 0; i < ctx->channel_count; i++) {
            ChannelInfo *ch = &ctx->channels[i];
            if (!ch->valid) continue;
            
            json_array_item_begin_object(w);
            json_write(w, "name", ch->name);
            json_write(w, "quantity", ch->quantity);
            json_write(w, "min", ch->min);
            json_write(w, "max", ch->max);
            if (ch->unit[0]) json_write(w, "unit", ch->unit);
            json_end_object(w);
        }
        json_end_array(w);
    }

    // Enhancements (objeto con gamma, clahe, etc.)
    if (ctx->count > 0) {
        json_begin_object(w, "enhancements");
        for (int i = 0; i < ctx->count; i++) {
            KeyVal *kv = &ctx->extra_fields[i];
            if (kv->type == 0) json_write(w, kv->key, kv->val_d);
            else if (kv->type == 1) json_write(w, kv->key, kv->val_s);
            else if (kv->type == 2) json_write_int(w, kv->key, (int)kv->val_d);
            else if (kv->type == 3) json_write_bool(w, kv->key, (bool)kv->val_d);
        }
        json_end_object(w);
    }

    json_close(w);
    return 0;
}
