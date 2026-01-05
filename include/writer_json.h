#ifndef WRITER_JSON_H
#define WRITER_JSON_H

#include <stdbool.h>

typedef struct JsonWriter JsonWriter;

// Ciclo de vida
JsonWriter* json_create(const char* filename);
void json_close(JsonWriter* writer);

// Estructura
void json_begin_object(JsonWriter* w, const char* key);
void json_end_object(JsonWriter* w);
void json_begin_array(JsonWriter* w, const char* key);
void json_end_array(JsonWriter* w);

// --- Funciones Específicas (Backend) ---
void json_write_string(JsonWriter* w, const char* key, const char* val);
void json_write_double(JsonWriter* w, const char* key, double val);
void json_write_int(JsonWriter* w, const char* key, int val);
void json_write_bool(JsonWriter* w, const char* key, bool val);

void json_write_float_array(JsonWriter* w, const char* key, const float* vals, int count);

// --- La Magia de C17: Macro Polimórfica ---
// El compilador sustituye 'json_write' por la función específica según el tipo de VAL.
#define json_write(W, KEY, VAL) \
    _Generic((VAL), \
        _Bool:       json_write_bool,   \
        int:         json_write_int,    \
        double:      json_write_double, \
        float:       json_write_double, \
        char*:       json_write_string, \
        const char*: json_write_string  \
    )(W, KEY, VAL)

#endif
