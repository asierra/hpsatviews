/*
 * Implementación de utilidades para la manipulación de nombres de archivo.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "filename_utils.h"
#include "logger.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

// --- Nuevo mecanismo de generación de nombres de archivo ---

// --- Funciones auxiliares estáticas ---

/**
 * @brief Extrae el nombre del satélite del nombre de archivo (formato "GXX").
 * @param filename Nombre del archivo de entrada.
 * @return String duplicado con el nombre del satélite, o NULL si no se encuentra.
 */
static char* extract_satellite_from_filename(const char* filename) {
    if (!filename) return NULL;
    
    const char* sat_ptr = strstr(filename, "_G");
    if (sat_ptr && strlen(sat_ptr) >= 4 && isdigit(sat_ptr[2]) && isdigit(sat_ptr[3])) {
        char buffer[4];
        snprintf(buffer, sizeof(buffer), "G%c%c", sat_ptr[2], sat_ptr[3]);
        return strdup(buffer);
    }
    return strdup("GXX"); // Fallback
}

/**
 * @brief Convierte fecha/hora a día juliano.
 */
static int date_to_julian(int year, int month, int day) {
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // Leap year check
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[2] = 29;
    }
    
    int jday = day;
    for (int m = 1; m < month; m++) {
        jday += days_in_month[m];
    }
    return jday;
}

/**
 * @brief Formatea el timestamp desde metadatos DataNC (formato: YYYYJJJ_hhmm).
 */
static void format_instant_from_datanc(const DataNC* datanc, char* buffer, size_t size) {
    if (!datanc) {
        snprintf(buffer, size, "NA");
        return;
    }
    
    int jday = date_to_julian(datanc->year, datanc->mon, datanc->day);
    snprintf(buffer, size, "%04d%03d_%02d%02d", 
             datanc->year, jday, datanc->hour, datanc->min);
}

/**
 * @brief Determina la parte <TIPO> del nombre de archivo.
 * @param info Puntero a la estructura de información.
 * @param buffer Buffer de salida.
 * @param size Tamaño del buffer.
 */
static void get_type_part(const FilenameGeneratorInfo* info, char* buffer, size_t size) {
    if (strcmp(info->command, "gray") == 0) {
        snprintf(buffer, size, "gray");
    } else if (strcmp(info->command, "pseudocolor") == 0) {
        snprintf(buffer, size, "pseudo");
    } else if (strcmp(info->command, "rgb") == 0) {
        if (info->rgb_mode && strcmp(info->rgb_mode, "truecolor") != 0 && strcmp(info->rgb_mode, "composite") != 0) {
            // Producto semántico: usar el nombre del modo
            snprintf(buffer, size, "%s", info->rgb_mode);
        } else {
            // RGB sin producto semántico
            snprintf(buffer, size, "rgb");
        }
    } else {
        snprintf(buffer, size, "NA");
    }
}

/**
 * @brief Determina la parte <BANDAS> del nombre de archivo.
 * @param info Puntero a la estructura de información.
 * @param buffer Buffer de salida.
 * @param size Tamaño del buffer.
 */
static void get_bands_part(const FilenameGeneratorInfo* info, char* buffer, size_t size) {
    if (strcmp(info->command, "gray") == 0 || strcmp(info->command, "pseudocolor") == 0) {
        if (info->datanc && info->datanc->band_id > 0) {
            // Detectar si es modo álgebra de bandas (expr)
            // Heurística: si band_id==0, pero estamos en gray, es expr; pero mejor: si el nombre de banda es NA y command==gray, es expr
            // Pero aquí solo tenemos datanc, así que usamos band_id==0 como señal de expr
            if (info->datanc->band_id == 0 && strcmp(info->command, "gray") == 0) {
                snprintf(buffer, size, "C_expr");
            } else {
                snprintf(buffer, size, "C%02d", info->datanc->band_id);
            }
        } else {
            // Si no hay band_id, pero es gray y álgebra, poner _expr
            if (strcmp(info->command, "gray") == 0) {
                snprintf(buffer, size, "C_expr");
            } else {
                snprintf(buffer, size, "NA");
            }
        }
    } else if (strcmp(info->command, "rgb") == 0) {
        if (info->rgb_mode && strcmp(info->rgb_mode, "truecolor") != 0 && strcmp(info->rgb_mode, "composite") != 0) {
            // Producto semántico
            snprintf(buffer, size, "auto");
        } else {
            // RGB explícito
            snprintf(buffer, size, "C02-C03-C01");
        }
    } else {
        snprintf(buffer, size, "NA");
    }
}

/**
 * @brief Construye la cadena de operaciones <OPS>.
 * @param info Puntero a la estructura de información.
 * @param buffer Buffer de salida.
 * @param size Tamaño del buffer.
 * @return true si se añadió alguna operación, false en caso contrario.
 */
static bool build_ops_string(const FilenameGeneratorInfo* info, char* buffer, size_t size) {
    buffer[0] = '\0';
    bool has_ops = false;

    const char* ops_list[6];
    int op_count = 0;
    char gamma_str[16];

    if (info->apply_rayleigh) ops_list[op_count++] = "ray";
    if (info->apply_histogram) ops_list[op_count++] = "histo";
    if (info->apply_clahe) ops_list[op_count++] = "clahe";
    if (fabsf(info->gamma - 1.0f) > 0.01f) {
        snprintf(gamma_str, sizeof(gamma_str), "g%.1f", info->gamma);
        for (char *p = gamma_str; *p; ++p) {
            if (*p == '.') *p = 'p';
        }
        ops_list[op_count++] = gamma_str;
    }
    if (info->has_clip) ops_list[op_count++] = "clip";
    if (info->do_reprojection) ops_list[op_count++] = "geo";

    if (op_count == 0) {
        return false;
    }

    // Construir la cadena final
    size_t current_len = 0;
    for (int i = 0; i < op_count; i++) {
        size_t op_len = strlen(ops_list[i]);
        // Verificar si hay espacio suficiente (incluyendo separador y terminador nulo)
        if (current_len + op_len + (i > 0 ? 2 : 0) + 1 < size) {
            if (i > 0) {
                strcat(buffer, "__");
                current_len += 2;
            }
            strcat(buffer, ops_list[i]);
            current_len += op_len;
            has_ops = true;
        } else {
            LOG_WARN("Buffer insuficiente para construir la cadena de operaciones completa.");
            break;
        }
    }
    return has_ops;
}

// --- Funciones para expansión de patrones ---

static void julian_to_date(int year, int jday, int *month, int *day) {
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // Leap year check
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) { // LCOV_EXCL_LINE
        days_in_month[2] = 29;
    }

    *month = 1;
    while (*month <= 12 && jday > days_in_month[*month]) {
        jday -= days_in_month[*month];
        (*month)++;
    }
    *day = jday;
}

// Helper to replace all occurrences of a substring
static char* str_replace(char *orig, const char *rep, const char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    // Allocate memory for result
    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

char* expand_filename_pattern(const char* pattern, const char* input_filename) {
    if (!pattern) return NULL;
    if (!input_filename) return strdup(pattern);

    // Extract channel number (e.g., "C01", "C02", "C13")
    char channel[4] = "C00";
    const char* ch_ptr = strstr(input_filename, "M6C");
    if (!ch_ptr) ch_ptr = strstr(input_filename, "M3C");
    if (ch_ptr) {
        ch_ptr += 3; // Skip "M6C" or "M3C"
        if (ch_ptr[0] >= '0' && ch_ptr[0] <= '9' && ch_ptr[1] >= '0' && ch_ptr[1] <= '9') {
            channel[0] = 'C';
            channel[1] = ch_ptr[0];
            channel[2] = ch_ptr[1];
            channel[3] = '\0';
        }
    }

    // Extract satellite name (e.g., "G16", "G19")
    char satellite[4] = "GXX";
    const char* sat_ptr = strstr(input_filename, "_G");
    if (sat_ptr) {
        sat_ptr += 2; // Skip "_G"
        if (sat_ptr[0] >= '0' && sat_ptr[0] <= '9' && sat_ptr[1] >= '0' && sat_ptr[1] <= '9') {
            sprintf(satellite, "G%c%c", sat_ptr[0], sat_ptr[1]);
        }
    }

    // Extract timestamp: sYYYYJJJHHMMSS
    const char* s_prefix = "_s";
    const char* start_ptr = strstr(input_filename, s_prefix);
    
    // If not found, return copy of pattern
    if (!start_ptr) return strdup(pattern);

    start_ptr += 2; // Skip "_s"
    
    // Parse YYYYJJJHHMMSS (at least 11 chars for YYYYJJJHHMM)
    if (strlen(start_ptr) < 11) return strdup(pattern);

    char s_year[5], s_jday[4], s_hour[3], s_min[3], s_sec[3], s_timestamp[12];
    strncpy(s_year, start_ptr, 4); s_year[4] = 0;
    strncpy(s_jday, start_ptr + 4, 3); s_jday[3] = 0;
    strncpy(s_hour, start_ptr + 7, 2); s_hour[2] = 0;
    strncpy(s_min, start_ptr + 9, 2); s_min[2] = 0;
    strncpy(s_timestamp, start_ptr, 11); s_timestamp[11] = 0;
    
    // Seconds might be present
    if (strlen(start_ptr) >= 13 && start_ptr[11] >= '0' && start_ptr[11] <= '9') {
        strncpy(s_sec, start_ptr + 11, 2); s_sec[2] = 0;
    } else {
        strcpy(s_sec, "00");
    }

    int year = atoi(s_year);
    int jday = atoi(s_jday);
    int month = 1, day = 1;
    julian_to_date(year, jday, &month, &day);

    char s_month[3], s_day[3];
    snprintf(s_month, sizeof(s_month), "%02d", month);
    snprintf(s_day, sizeof(s_day), "%02d", day);
    
    char s_yy[3];
    strncpy(s_yy, s_year + 2, 2); s_yy[2] = 0;

    // Perform replacements sequentially
    char *current = strdup(pattern);
    char *next;

    struct {
        const char* key;
        const char* val;
    } replacements[] = {
        {"{YYYY}", s_year},
        {"{YY}", s_yy},
        {"{MM}", s_month},
        {"{DD}", s_day},
        {"{hh}", s_hour},
        {"{mm}", s_min},
        {"{ss}", s_sec},
        {"{TS}", s_timestamp},
        {"{JJJ}", s_jday},
        {"{CH}", channel},
        {"{SAT}", satellite},
        {NULL, NULL}
    };

    for (int i = 0; replacements[i].key != NULL; i++) {
        next = str_replace(current, replacements[i].key, replacements[i].val);
        free(current);
        if (!next) return NULL; // Should not happen unless malloc fails
        current = next;
    }

    return current;
}

/**
 * @brief Genera un nombre de archivo estandarizado y descriptivo basado en los parámetros de procesamiento.
 * 
 * Usa metadatos de DataNC (fecha/hora, banda) en lugar de parsear el nombre de archivo.
 * Formato: hpsv_<SAT>_<YYYYJJJ_hhmm>_<TIPO>_<BANDAS>[_<OPS>].<ext>
 */
char* generate_hpsv_filename(const FilenameGeneratorInfo* info) {
    if (!info || !info->command) {
        LOG_ERROR("Información insuficiente para generar nombre de archivo.");
        return NULL;
    }

    // Satélite (de filename si está disponible, sino "GXX")
    const char* sat = info->satellite_name ? info->satellite_name : "GXX";
    
    // Timestamp desde DataNC
    char instant[20] = "NA";
    format_instant_from_datanc(info->datanc, instant, sizeof(instant));
    
    // Tipo de producto
    char type[32] = "NA";
    get_type_part(info, type, sizeof(type));
    
    // Bandas
    char bands[32] = "NA";
    get_bands_part(info, bands, sizeof(bands));
    
    // Operaciones aplicadas
    char ops[128] = "";
    bool has_ops = build_ops_string(info, ops, sizeof(ops));
    
    // Extensión
    const char* ext = info->force_geotiff ? "tif" : "png";

    // Construir nombre de archivo
    char filename_buffer[512];
    if (has_ops) {
        snprintf(filename_buffer, sizeof(filename_buffer), "hpsv_%s_%s_%s_%s_%s.%s",
                 sat, instant, type, bands, ops, ext);
    } else {
        snprintf(filename_buffer, sizeof(filename_buffer), "hpsv_%s_%s_%s_%s.%s",
                 sat, instant, type, bands, ext);
    }

    return strdup(filename_buffer);
}

/**
 * @brief Extrae el nombre del satélite del nombre de archivo GOES.
 * @param filename Nombre del archivo (puede incluir ruta).
 * @return String duplicado con formato "goes-XX" o NULL si falla.
 */
char* extract_satellite_name(const char* filename) {
    return extract_satellite_from_filename(filename);
}

/**
 * @brief Genera un nombre de archivo de salida por defecto.
 * @deprecated Esta función es obsoleta y será eliminada. Usar generate_hpsv_filename en su lugar.
 */
char* generate_default_output_filename(const char* input_file_path, const char* processing_mode, const char* output_extension) {
    (void)input_file_path;
    (void)processing_mode;
    (void)output_extension;
    LOG_WARN("Llamada a función obsoleta 'generate_default_output_filename'. Usar 'generate_hpsv_filename' en su lugar.");
    return strdup("hpsv_output_fallback.png");
}
