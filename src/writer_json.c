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

static void check_comma(JsonWriter* w) {
    if (w->needs_comma[w->depth]) {
        fprintf(w->fp, ",\n");
    } else {
        if (w->depth > 0) fprintf(w->fp, "\n");
        w->needs_comma[w->depth] = true;
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

// --- Implementación Pública ---

JsonWriter* json_create(const char* filename) {
    JsonWriter* w = calloc(1, sizeof(JsonWriter));
    if (!w) return NULL;
    w->fp = fopen(filename, "w");
    if (!w->fp) {
        free(w);
        return NULL;
    }
    fprintf(w->fp, "{"); // Abrir JSON raíz
    return w;
}

void json_close(JsonWriter* w) {
    if (w) {
        if (w->fp) {
            fprintf(w->fp, "\n}\n"); // Cerrar JSON raíz
            fclose(w->fp);
        }
        free(w);
    }
}

void json_begin_object(JsonWriter* w, const char* key) {
    write_key(w, key);
    fprintf(w->fp, "{");
    w->depth++;
    if (w->depth < MAX_DEPTH) w->needs_comma[w->depth] = false;
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
    if (w->depth < MAX_DEPTH) w->needs_comma[w->depth] = false;
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
        fprintf(w->fp, "%.6g%s", vals[i], (i < count - 1) ? ", " : "");
    }
    fprintf(w->fp, "]");
}

// --- Funciones para Array Items (sin clave) ---

void json_array_item_begin_object(JsonWriter* w) {
    check_comma(w);
    fprintf(w->fp, "{");
    w->depth++;
    if (w->depth < MAX_DEPTH) w->needs_comma[w->depth] = false;
}

void json_array_item_string(JsonWriter* w, const char* val) {
    check_comma(w);
    print_escaped_string(w->fp, val ? val : "");
}
