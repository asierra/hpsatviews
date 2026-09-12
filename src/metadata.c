/* Product metadata aggregation and JSON sidecar serialization.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "metadata.h"
#include "writer_json.h"
#include "datanc.h"
#include "logger.h"
#include "version.h"
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

#define MAX_ASSETS 4

typedef struct {
    char key[32];           // "image", "image_geographic"
    char href[512];         // path as written; NOT via KeyVal, which truncates at 63
    char media_type[64];
    int width, height;
    double transform[6];
    bool has_transform;
    int epsg;
    char *wkt2;
} AssetInfo;

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
    const char *sector;
    char time_iso[32];
    time_t timestamp;
    char product[128];
    
    double bbox[4];
    char projection[32];
    bool has_bbox;
    Footprint footprint;
    bool has_footprint;

    double transform[6];
    int shape[2];          /* [height, width], STAC's proj:shape order */
    char *wkt2;
    int epsg;
    bool has_grid;
    bool has_clip;  // true only when the user specified an explicit clip region
    
    ChannelInfo channels[MAX_CHANNELS];
    int channel_count;

    AssetInfo assets[MAX_ASSETS];
    int asset_count;
    
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

static const char *SECTOR_NAMES[] = {
    [SECTOR_UNKNOWN] = "",
    [SECTOR_FD]      = "fd",
    [SECTOR_CONUS]   = "conus",
    [SECTOR_M1]      = "m1",
    [SECTOR_M2]      = "m2",
};

const char* metadata_sat_name(SatelliteID id) {
    if (id >= SAT_UNKNOWN && id <= SAT_GOES19) {
        return SAT_NAMES[id];
    }
    return "unknown";
}

const char* metadata_sector_name(SectorID id) {
    if (id >= SECTOR_UNKNOWN && id <= SECTOR_M2) {
        return SECTOR_NAMES[id];
    }
    return "";
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
    if (ctx) {
        free(ctx->wkt2);
        for (int i = 0; i < ctx->asset_count; i++) free(ctx->assets[i].wkt2);
    }
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

    // 2. Copy satellite name.
	ctx->satellite = metadata_sat_name(nc->sat_id);
	LOG_DEBUG("Satellite ID %d name %s", nc->sat_id, ctx->satellite);

    // 3. Sector
    if (nc->sector_id >= SECTOR_UNKNOWN && nc->sector_id <= SECTOR_M2) {
        ctx->sector = SECTOR_NAMES[nc->sector_id];
    }

    // 4. Add channel metadata.
    if (ctx->channel_count < MAX_CHANNELS && nc->varname) {
        ChannelInfo *ch = &ctx->channels[ctx->channel_count];
        
        // Use band_id (e.g., "C13") if available, otherwise fall back to varname.
        if (nc->band_id > 0 && nc->band_id <= 16) {
            snprintf(ch->name, sizeof(ch->name), "C%02d", nc->band_id);
        } else {
            strncpy(ch->name, nc->varname, sizeof(ch->name) - 1);
        }
        
        // Determinar quantity basado en el tipo de datos
        if (nc->is_float) {
            ch->min = nc->fdata.fmin;
            ch->max = nc->fdata.fmax;
            
            if (nc->band_id >= 7) {
                strncpy(ch->quantity, "brightness_temperature", sizeof(ch->quantity) - 1);
                strncpy(ch->unit, "K", sizeof(ch->unit) - 1);
            } else {
                strncpy(ch->quantity, "reflectance", sizeof(ch->quantity) - 1);
                strncpy(ch->unit, "unitless", sizeof(ch->unit) - 1);
            }
        } else {
            ch->min = nc->bdata.min;
            ch->max = nc->bdata.max;
            // Para L2 con categorías o productos derivados
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

void metadata_set_product(MetadataContext *ctx, const char *product) {
    if (!ctx || !product) return;
    strncpy(ctx->product, product, sizeof(ctx->product) - 1);
    ctx->product[sizeof(ctx->product) - 1] = '\0';
}

void metadata_set_projection(MetadataContext *ctx, const char *proj) {
    if (!ctx || !proj) return;
    strncpy(ctx->projection, proj, sizeof(ctx->projection) - 1);
}

void metadata_set_geometry(MetadataContext *ctx, double x1, double y1, double x2, double y2) {
    if(!ctx) return;
    ctx->bbox[0] = x1; ctx->bbox[1] = y1;
    ctx->bbox[2] = x2; ctx->bbox[3] = y2;
    ctx->has_bbox = true;
}

void metadata_set_footprint(MetadataContext *ctx, const Footprint *fp) {
    if (!ctx || !fp || !fp->valid) return;
    ctx->footprint = *fp;
    ctx->has_footprint = true;
}

void metadata_set_grid(MetadataContext *ctx, const double transform[6],
                       int width, int height, const char *wkt2, int epsg) {
    if (!ctx || !transform || width <= 0 || height <= 0) return;
    memcpy(ctx->transform, transform, sizeof(ctx->transform));
    ctx->shape[0] = height;
    ctx->shape[1] = width;
    ctx->epsg = epsg;
    free(ctx->wkt2);
    ctx->wkt2 = NULL;
    if (wkt2 && wkt2[0]) {
        ctx->wkt2 = malloc(strlen(wkt2) + 1);
        if (ctx->wkt2) strcpy(ctx->wkt2, wkt2);
    }
    ctx->has_grid = true;
}

const char* metadata_media_type(bool is_geotiff, bool cog) {
    if (!is_geotiff) return "image/png";
    // Without --cog the COG driver still runs, just without the overview
    // pyramids (writer_geotiff.c), so the profile must not be claimed.
    return cog ? "image/tiff; application=geotiff; profile=cloud-optimized"
               : "image/tiff; application=geotiff";
}

void metadata_add_asset(MetadataContext *ctx, const char *key, const char *href,
                        const char *media_type, int width, int height,
                        const double transform[6], int epsg, const char *wkt2) {
    if (!ctx || !key || !href) return;
    for (int i = 0; i < ctx->asset_count; i++) {
        if (strcmp(ctx->assets[i].key, key) == 0) return;  /* first write wins */
    }
    if (ctx->asset_count >= MAX_ASSETS) {
        LOG_WARN("More than %d outputs in one run; '%s' is missing from the Item.",
                 MAX_ASSETS, key);
        return;
    }
    AssetInfo *a = &ctx->assets[ctx->asset_count++];
    snprintf(a->key, sizeof(a->key), "%s", key);
    // Relative to the Item, which sits in the same directory: the tool does not
    // know the publication URL, and a bare name keeps resolving wherever the
    // pair is copied. An absolute local path would be meaningless once served.
    const char *base = strrchr(href, '/');
    snprintf(a->href, sizeof(a->href), "%s", base ? base + 1 : href);
    snprintf(a->media_type, sizeof(a->media_type), "%s", media_type ? media_type : "");
    a->width = width;
    a->height = height;
    a->epsg = epsg;
    a->has_transform = (transform != NULL);
    if (transform) memcpy(a->transform, transform, sizeof(a->transform));
    a->wkt2 = NULL;
    if (wkt2 && wkt2[0]) {
        a->wkt2 = malloc(strlen(wkt2) + 1);
        if (a->wkt2) strcpy(a->wkt2, wkt2);
    }
}

void metadata_set_clip(MetadataContext *ctx, bool clipped) {
    if (!ctx) return;
    ctx->has_clip = clipped;
}

// Scalar/string/int/bool adder implementations.
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
    metadata_add_dbl(c, k, (double)v);
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
    bool has_rayleigh = false, has_invert = false, has_stretch = false;
    float gamma_val = 1.0f;
    
    for (int i = 0; i < ctx->count; i++) {
        const KeyVal *kv = &ctx->extra_fields[i];
        if (strcmp(kv->key, "gamma") == 0 && kv->type == 0) {
            gamma_val = (float)kv->val_d;
            if (fabsf(gamma_val - 1.0f) > 0.01f) has_gamma = true;
        } else if (strcmp(kv->key, "clahe") == 0 && kv->type != 1) {
            has_clahe = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "histogram") == 0 && kv->type != 1) {
            has_histo = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "rayleigh") == 0 && kv->type != 1) {
            has_rayleigh = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "invert") == 0 && kv->type != 1) {
            has_invert = (kv->val_d != 0.0);
        } else if (strcmp(kv->key, "stretch") == 0 && kv->type != 1) {
            has_stretch = (kv->val_d != 0.0);
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
    if (ctx->has_clip) ops_list[op_count++] = "clip";
    if (has_stretch) ops_list[op_count++] = "str";
    
    // Buscar "reprojection" o "geographics" en extra_fields
    for (int i = 0; i < ctx->count; i++) {
        if ((strcmp(ctx->extra_fields[i].key, "reprojection") == 0 ||
             strcmp(ctx->extra_fields[i].key, "geographics") == 0) &&
            ctx->extra_fields[i].type != 1 && ctx->extra_fields[i].val_d != 0.0) {
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

/* The scene-and-product part of the name: hpsv_<SAT>[_<SECTOR>]_<YYYYJJJ_hhmm>
 * _<TYPE>[_<BANDS>]. Shared by the output filename, which appends the applied
 * operations, and by the Item id, which must not carry them: two renderings of
 * one scene are one item with two assets, not two items (D1). */
static void build_stem(const MetadataContext *ctx, char *buf, size_t size) {
    const char *sat = (ctx->satellite && ctx->satellite[0]) ? ctx->satellite : "GXX";
    const char *sector = (ctx->sector && ctx->sector[0]) ? ctx->sector : NULL;

    char instant[20];
    format_timestamp_julian(ctx->timestamp, instant, sizeof(instant));

    char type[64] = "output";
    if (ctx->command[0]) {
        if (strcmp(ctx->command, "gray") == 0) {
            strcpy(type, "gray");
        } else if (strcmp(ctx->command, "pseudocolor") == 0) {
            strcpy(type, "pseudo");
        } else if (strcmp(ctx->command, "rgb") == 0) {
            const char *mode = NULL;
            for (int i = 0; i < ctx->count; i++) {
                if (strcmp(ctx->extra_fields[i].key, "mode") == 0 &&
                    ctx->extra_fields[i].type == 1) {
                    mode = ctx->extra_fields[i].val_s;
                    break;
                }
            }
            if (mode && mode[0] && strcmp(mode, "truecolor") != 0 && strcmp(mode, "composite") != 0)
                snprintf(type, sizeof(type), "%s", mode);
            else
                strcpy(type, "rgb");
        } else {
            strncpy(type, ctx->command, sizeof(type) - 1);
        }
    }

    char bands[32] = "";
    if (strcmp(ctx->command, "rgb") == 0) {
        // Semantic RGB modes: bands already encoded in the type field.
        bands[0] = '\0';
    } else if (ctx->channel_count > 0 && ctx->channels[0].valid) {
        strncpy(bands, ctx->channels[0].name, sizeof(bands) - 1);
    }

    char sat_prefix[32];
    if (sector) snprintf(sat_prefix, sizeof(sat_prefix), "%s_%s", sat, sector);
    else        snprintf(sat_prefix, sizeof(sat_prefix), "%s", sat);

    if (bands[0])
        snprintf(buf, size, "hpsv_%s_%s_%s_%s", sat_prefix, instant, type, bands);
    else
        snprintf(buf, size, "hpsv_%s_%s_%s", sat_prefix, instant, type);
}

char* metadata_build_id(const MetadataContext *ctx) {
    if (!ctx) return NULL;
    char *id = malloc(512);
    if (!id) return NULL;
    build_stem(ctx, id, 512);
    return id;
}

char* metadata_build_filename(const MetadataContext *ctx, const char *extension) {
    if (!ctx || !extension) return NULL;

    char *filename = malloc(512);
    if (!filename) return NULL;

    char stem[384];
    build_stem(ctx, stem, sizeof(stem));

    char ops[128] = "";
    bool has_ops = build_ops_string(ctx, ops, sizeof(ops));

    if (has_ops) snprintf(filename, 512, "%s_%s%s", stem, ops, extension);
    else         snprintf(filename, 512, "%s%s", stem, extension);

    return filename;
}

/* Versions of the core spec and of each extension. They are a contract with
 * every consumer and they are NOT inherited from the day this was written:
 * phase 5 of docs/stac/STAC_PLAN.md revalidates them against the live schemas.
 * Kept together so that revalidation is one edit. */
#define STAC_VERSION "1.0.0"
#define STAC_EXT_PROJ "https://stac-extensions.github.io/projection/v1.1.0/schema.json"
#define STAC_EXT_EO "https://stac-extensions.github.io/eo/v1.1.0/schema.json"
#define STAC_EXT_PROCESSING "https://stac-extensions.github.io/processing/v1.1.0/schema.json"

/* Canonical platform name. The sidecar's "G16" is our own shorthand; STAC's
 * platform field wants the published identifier. */
static const char* stac_platform(const char *satellite) {
    if (!satellite) return NULL;
    if (strcmp(satellite, "G16") == 0) return "goes-16";
    if (strcmp(satellite, "G17") == 0) return "goes-17";
    if (strcmp(satellite, "G18") == 0) return "goes-18";
    if (strcmp(satellite, "G19") == 0) return "goes-19";
    return NULL;
}

/* ABI band centres in µm, indexed by band number. Taken from the table in
 * docs/stac/STAC_PLAN.md, which still carries the note to check them against
 * the ABI PUG. */
static const double kBandCentre[17] = {
    0.0, 0.47, 0.64, 0.86, 1.37, 1.6, 2.24, 3.9, 6.2,
    6.9, 7.3, 8.4, 9.6, 10.3, 11.2, 12.3, 13.3
};

/* STAC's eo common names only cover the reflective bands unambiguously; there
 * is no agreed name for the ABI thermal channels, and inventing one would
 * assert something false to anybody filtering on it. Those simply go without. */
static const char* stac_common_name(int band) {
    switch (band) {
        case 1: return "blue";
        case 2: return "red";
        case 3: return "nir08";
        case 4: return "cirrus";
        case 5: return "swir16";
        case 6: return "swir22";
        default: return NULL;
    }
}

/// Band number from a "C13"-style channel name; 0 when it is not one.
static int band_number(const char *name) {
    if (!name || name[0] != 'C') return 0;
    int n = atoi(name + 1);
    return (n >= 1 && n <= 16) ? n : 0;
}

int metadata_save_stac_item(MetadataContext *ctx, const char *filename,
                            const char *collection) {
    if (!ctx) return -1;

    // A STAC Item without geometry is valid but unsearchable, which defeats the
    // whole point of emitting one. Fail loudly instead of writing an orphan.
    if (!ctx->has_footprint) {
        LOG_ERROR("No geographic footprint for this output, so no STAC Item can be written. "
                  "The file carries no usable projection.");
        return -1;
    }

    char *id = metadata_build_id(ctx);
    if (!id) return -1;

    JsonWriter *w = json_create(filename);
    if (!w) { free(id); return -1; }

    json_write(w, "type", "Feature");
    json_write(w, "stac_version", STAC_VERSION);

    json_begin_array(w, "stac_extensions");
    json_array_item_string(w, STAC_EXT_PROJ);
    json_array_item_string(w, STAC_EXT_EO);
    json_array_item_string(w, STAC_EXT_PROCESSING);
    json_end_array(w);

    json_write(w, "id", id);
    if (collection && collection[0]) json_write(w, "collection", collection);

    json_write_polygon(w, "geometry", ctx->footprint.lon, ctx->footprint.lat,
                       ctx->footprint.count);
    json_write_double_array(w, "bbox", ctx->footprint.bbox, 4);

    json_begin_object(w, "properties");
    if (ctx->time_iso[0]) json_write(w, "datetime", ctx->time_iso);

    const char *platform = stac_platform(ctx->satellite);
    if (platform) {
        json_write(w, "platform", platform);
        json_write(w, "constellation", "goes");
        json_begin_array(w, "instruments");
        json_array_item_string(w, "abi");
        json_end_array(w);
    }

    json_begin_object(w, "processing:software");
    json_write(w, "hpsatviews", HPSV_VERSION);
    json_end_object(w);

    if (ctx->has_grid) {
        if (ctx->epsg > 0) json_write_int(w, "proj:epsg", ctx->epsg);
        if (ctx->wkt2) json_write_string(w, "proj:wkt2", ctx->wkt2);
        json_write_double_array(w, "proj:transform", ctx->transform, 6);
        json_write_int_array(w, "proj:shape", ctx->shape, 2);
    }
    // The box in the output's own CRS: metres on the fixed grid, degrees once
    // reprojected. The root bbox is always 4326, so this is not a duplicate.
    if (ctx->has_bbox) json_write_double_array(w, "proj:bbox", ctx->bbox, 4);

    if (ctx->channel_count > 0) {
        json_begin_array(w, "eo:bands");
        for (int i = 0; i < ctx->channel_count; i++) {
            ChannelInfo *ch = &ctx->channels[i];
            if (!ch->valid) continue;
            json_array_item_begin_object(w);
            json_write(w, "name", ch->name);
            int band = band_number(ch->name);
            const char *common = stac_common_name(band);
            if (common) json_write(w, "common_name", common);
            if (band > 0) json_write(w, "center_wavelength", kBandCentre[band]);
            json_end_object(w);
        }
        json_end_array(w);
    }

    if (ctx->sector && ctx->sector[0]) json_write(w, "hpsv:sector", ctx->sector);
    if (ctx->product[0]) json_write(w, "hpsv:product", ctx->product);
    if (ctx->command[0]) json_write(w, "hpsv:command", ctx->command);

    // Physical ranges hang off the item, not off raster:bands of an asset: the
    // assets are 8-bit renderings, and attaching kelvin statistics to them
    // would assert something false that no validator would catch.
    if (ctx->channel_count > 0) {
        json_begin_array(w, "hpsv:channels");
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

    if (ctx->count > 0) {
        json_begin_object(w, "hpsv:enhancements");
        for (int i = 0; i < ctx->count; i++) {
            KeyVal *kv = &ctx->extra_fields[i];
            // The written files are assets now, not enhancement parameters.
            if (strcmp(kv->key, "output_file") == 0 ||
                strcmp(kv->key, "output_width") == 0 ||
                strcmp(kv->key, "output_height") == 0) continue;
            if (kv->type == 0) json_write(w, kv->key, kv->val_d);
            else if (kv->type == 1) json_write(w, kv->key, kv->val_s);
            else if (kv->type == 2) json_write_int(w, kv->key, (int)kv->val_d);
            else if (kv->type == 3) json_write_bool(w, kv->key, (bool)kv->val_d);
        }
        json_end_object(w);
    }
    json_end_object(w);   /* properties */

    // The tool does not know where the file will be published, so it cannot
    // build self/root/parent links. An empty array is valid STAC; the indexer
    // completes the graph.
    json_begin_array(w, "links");
    json_end_array(w);

    json_begin_object(w, "assets");
    for (int i = 0; i < ctx->asset_count; i++) {
        AssetInfo *a = &ctx->assets[i];
        json_begin_object(w, a->key);
        json_write(w, "href", a->href);
        if (a->media_type[0]) json_write(w, "type", a->media_type);
        json_begin_array(w, "roles");
        json_array_item_string(w, "data");
        json_end_array(w);
        if (a->width > 0 && a->height > 0) {
            int shape[2] = {a->height, a->width};
            json_write_int_array(w, "proj:shape", shape, 2);
        }
        if (a->has_transform) {
            json_write_double_array(w, "proj:transform", a->transform, 6);
            if (a->epsg > 0) json_write_int(w, "proj:epsg", a->epsg);
            // Sin esto el activo de rejilla fija, que no tiene código EPSG,
            // hereda el CRS del Item y su origen en metros se lee como grados.
            if (a->wkt2) json_write_string(w, "proj:wkt2", a->wkt2);
        }
        json_end_object(w);
    }
    json_end_object(w);   /* assets */

    json_close(w);
    free(id);
    return 0;
}
