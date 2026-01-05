#include "metadata.h"
#include "writer_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_KV 32

typedef struct {
    char key[32];
    char val_s[64];
    double val_d;
    int type; // 0=dbl, 1=str, 2=int, 3=bool
} KeyVal;

struct MetadataContext {
    char satellite[32];
    char time_iso[32];
    float bbox[4];
    bool has_bbox;
    
    // Almacenamiento temporal simple para esta fase
    KeyVal extra_fields[MAX_KV];
    int count;
};

MetadataContext* metadata_create(void) {
    return calloc(1, sizeof(MetadataContext));
}

void metadata_destroy(MetadataContext *ctx) {
    free(ctx);
}

void metadata_set_satellite(MetadataContext *ctx, const char *sat) {
    if(ctx && sat) strncpy(ctx->satellite, sat, 31);
}

void metadata_set_time(MetadataContext *ctx, const char *iso) {
    if(ctx && iso) strncpy(ctx->time_iso, iso, 31);
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

int metadata_save_json(MetadataContext *ctx, const char *filename) {
    JsonWriter *w = json_create(filename);
    if (!w) return -1;

    json_write(w, "tool", "hpsatviews");
    if(ctx->satellite[0]) json_write(w, "satellite", ctx->satellite);
    if(ctx->time_iso[0])  json_write(w, "time_iso", ctx->time_iso);

    if (ctx->has_bbox) {
        json_begin_object(w, "geometry");
        json_write_float_array(w, "bbox", ctx->bbox, 4);
        json_end_object(w);
    }

    json_begin_object(w, "data");
    for(int i=0; i<ctx->count; i++) {
        KeyVal *kv = &ctx->extra_fields[i];
        if(kv->type == 0) json_write(w, kv->key, kv->val_d);
        else if(kv->type == 1) json_write(w, kv->key, kv->val_s);
        else if(kv->type == 2) json_write_int(w, kv->key, (int)kv->val_d);
        else if(kv->type == 3) json_write_bool(w, kv->key, (bool)kv->val_d);
    }
    json_end_object(w);

    json_close(w);
    return 0;
}
