/* Minimal JSON writer for metadata sidecar files.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "writer_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 32

struct JsonWriter {
    FILE* fp;
    int depth;
    bool needs_comma[MAX_DEPTH]; 
};

// --- Helpers Privados ---

static void write_indent(JsonWriter* w) {
    for (int i = 0; i < w->depth; i++) fprintf(w->fp, "  ");
}

/* depth is incremented unconditionally by the begin_* functions, so past
 * MAX_DEPTH nestings the raw index would run off the array. Clamping keeps the
 * access in bounds; beyond that depth only the comma bookkeeping degrades,
 * which no caller in this tree ever reaches (the sidecar nests three deep). */
static bool* comma_flag(JsonWriter* w) {
    int d = w->depth;
    if (d < 0) d = 0;
    else if (d >= MAX_DEPTH) d = MAX_DEPTH - 1;
    return &w->needs_comma[d];
}

static void check_comma(JsonWriter* w) {
    bool *flag = comma_flag(w);
    if (*flag) {
        fprintf(w->fp, ",\n");
    } else {
        if (w->depth > 0) fprintf(w->fp, "\n");
        *flag = true;
    }
    write_indent(w);
}

static void print_escaped_string(FILE* fp, const char* str) {
    fputc('"', fp);
    if (str) {
        for (const char* p = str; *p; p++) {
            switch (*p) {
                case '"':  fprintf(fp, "\\\""); break;
                case '\\': fprintf(fp, "\\\\"); break;
                case '\n': fprintf(fp, "\\n"); break;
                case '\r': fprintf(fp, "\\r"); break;
                case '\t': fprintf(fp, "\\t"); break;
                default:   fputc(*p, fp); break;
            }
        }
    }
    fputc('"', fp);
}

static void write_key(JsonWriter* w, const char* key) {
    check_comma(w);
    if (key) {
        print_escaped_string(w->fp, key);
        fprintf(w->fp, ": ");
    }
}

// --- Public API ---

JsonWriter* json_create(const char* filename) {
    JsonWriter* w = calloc(1, sizeof(JsonWriter));
    if (!w) return NULL;
    w->fp = fopen(filename, "w");
    if (!w->fp) {
        free(w);
        return NULL;
    }
    fprintf(w->fp, "{"); // open JSON root object
    return w;
}

void json_close(JsonWriter* w) {
    if (w) {
        if (w->fp) {
            fprintf(w->fp, "\n}\n"); // close JSON root object
            fclose(w->fp);
        }
        free(w);
    }
}

void json_begin_object(JsonWriter* w, const char* key) {
    write_key(w, key);
    fprintf(w->fp, "{");
    w->depth++;
    *comma_flag(w) = false;
}

void json_end_object(JsonWriter* w) {
    w->depth--;
    fprintf(w->fp, "\n");
    write_indent(w);
    fprintf(w->fp, "}");
}

void json_begin_array(JsonWriter* w, const char* key) {
    write_key(w, key);
    fprintf(w->fp, "[");
    w->depth++;
    *comma_flag(w) = false;
}

void json_end_array(JsonWriter* w) {
    w->depth--;
    fprintf(w->fp, "\n");
    write_indent(w);
    fprintf(w->fp, "]");
}

void json_write_string(JsonWriter* w, const char* key, const char* val) {
    write_key(w, key);
    print_escaped_string(w->fp, val ? val : "");
}

void json_write_double(JsonWriter* w, const char* key, double val) {
    write_key(w, key);
    fprintf(w->fp, "%.6g", val);
}

void json_write_int(JsonWriter* w, const char* key, int val) {
    write_key(w, key);
    fprintf(w->fp, "%d", val);
}

void json_write_bool(JsonWriter* w, const char* key, bool val) {
    write_key(w, key);
    fprintf(w->fp, val ? "true" : "false");
}

void json_write_float_array(JsonWriter* w, const char* key, const float* vals, int count) {
    write_key(w, key);
    fprintf(w->fp, "[");
    for (int i = 0; i < count; i++) {
        fprintf(w->fp, "%.8g%s", vals[i], (i < count - 1) ? ", " : "");
    }
    fprintf(w->fp, "]");
}

void json_write_double_array(JsonWriter* w, const char* key, const double* vals, int count) {
    write_key(w, key);
    fprintf(w->fp, "[");
    for (int i = 0; i < count; i++) {
        fprintf(w->fp, "%.10g%s", vals[i], (i < count - 1) ? ", " : "");
    }
    fprintf(w->fp, "]");
}

/* GeoJSON Polygon with a single ring, closed here (the caller keeps an open
 * ring). Coordinates go out as %.6f — about 11 cm, two orders below the
 * finest ABI pixel — and one pair per line would make a 128-vertex limb
 * unreadable, so the ring is written compactly. */
void json_write_polygon(JsonWriter* w, const char* key, const double* lon,
                        const double* lat, int count) {
    if (count < 3) return;
    json_begin_object(w, key);
    json_write_string(w, "type", "Polygon");
    write_key(w, "coordinates");
    fprintf(w->fp, "[[");
    for (int i = 0; i <= count; i++) {
        int j = (i < count) ? i : 0;     /* repeat the first vertex to close */
        fprintf(w->fp, "%s[%.6f, %.6f]", (i > 0) ? ", " : "", lon[j], lat[j]);
    }
    fprintf(w->fp, "]]");
    json_end_object(w);
}

// --- Funciones para Array Items (sin clave) ---

void json_array_item_begin_object(JsonWriter* w) {
    check_comma(w);
    fprintf(w->fp, "{");
    w->depth++;
    *comma_flag(w) = false;
}

void json_array_item_string(JsonWriter* w, const char* val) {
    check_comma(w);
    print_escaped_string(w->fp, val ? val : "");
}
