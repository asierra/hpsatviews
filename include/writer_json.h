/* Minimal JSON writer for metadata sidecar files.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_WRITER_JSON_H_
#define HPSATVIEWS_WRITER_JSON_H_

#include <stdbool.h>

typedef struct JsonWriter JsonWriter;

// Lifecycle
JsonWriter* json_create(const char* filename);
void json_close(JsonWriter* writer);

// Structure
void json_begin_object(JsonWriter* w, const char* key);
void json_end_object(JsonWriter* w);
void json_begin_array(JsonWriter* w, const char* key);
void json_end_array(JsonWriter* w);

void json_write_string(JsonWriter* w, const char* key, const char* val);
void json_write_double(JsonWriter* w, const char* key, double val);
void json_write_int(JsonWriter* w, const char* key, int val);
void json_write_bool(JsonWriter* w, const char* key, bool val);

void json_write_float_array(JsonWriter* w, const char* key, const float* vals, int count);

// Array element writers (no key)
void json_array_item_begin_object(JsonWriter* w);
void json_array_item_string(JsonWriter* w, const char* val);

// C11 _Generic polymorphic writer: json_write(w, "key", value).
#define json_write(W, KEY, VAL) \
    _Generic((VAL), \
        bool:        json_write_bool,   \
        int:         json_write_int,    \
        double:      json_write_double, \
        char*:       json_write_string, \
        const char*: json_write_string  \
    )(W, KEY, VAL)

#endif /* HPSATVIEWS_WRITER_JSON_H_ */
