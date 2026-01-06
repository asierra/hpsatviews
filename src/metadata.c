#include "metadata.h"
#include "writer_json.h"
#include "datanc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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
    char satellite[32];
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
    struct tm *tm_info = gmtime(&nc->timestamp);
    if (tm_info) {
        strftime(ctx->time_iso, sizeof(ctx->time_iso), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    }

    // 2. Copiar Satélite
    const char* sat_name = get_sat_name(nc->sat_id);
    metadata_set_satellite(ctx, sat_name);

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


void metadata_set_satellite(MetadataContext *ctx, const char *sat_name) {
    if (!ctx || !sat_name) return;
    strncpy(ctx->satellite, sat_name, sizeof(ctx->satellite) - 1);
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

char* metadata_build_filename(const MetadataContext *ctx, const char *extension) {
    if (!ctx || !extension) return NULL;
    
    // Formato: hpsatviews_SATELLITE_YYYYMMDD_HHMMSS.ext
    char *filename = malloc(256);
    if (!filename) return NULL;
    
    struct tm *tm_info = gmtime(&ctx->timestamp);
    char date_str[32] = "unknown";
    if (tm_info) {
        strftime(date_str, sizeof(date_str), "%Y%m%d_%H%M%S", tm_info);
    }
    
    const char *sat = ctx->satellite[0] ? ctx->satellite : "unknown";
    const char *cmd = ctx->command[0] ? ctx->command : "output";
    
    snprintf(filename, 256, "hpsatviews_%s_%s_%s%s", sat, cmd, date_str, extension);
    return filename;
}

int metadata_save_json(MetadataContext *ctx, const char *filename) {
    JsonWriter *w = json_create(filename);
    if (!w) return -1;

    // Campos requeridos del schema
    json_write(w, "tool", ctx->tool[0] ? ctx->tool : "hpsatviews");
    json_write(w, "version", "1.0");  // TODO: usar HPSV_VERSION_STRING
    if (ctx->command[0]) json_write(w, "command", ctx->command);
    if (ctx->satellite[0]) json_write(w, "satellite", ctx->satellite);
    if (ctx->time_iso[0]) json_write(w, "timestamp_iso", ctx->time_iso);

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
