/*
 * Implementación de utilidades para la manipulación de nombres de archivo.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "filename_utils.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

/**
 * @brief Extrae la firma temporal YYYYJJJHHMM de un nombre de archivo GOES L1b.
 *        Se espera un formato como "..._sYYYYJJJHHMMSS...".
 * @param filename La ruta completa o el nombre del archivo de entrada.
 * @return Una cadena de caracteres asignada dinámicamente que contiene la firma temporal
 *         (ej. "20253231800"), o NULL si no se encuentra. El llamador debe liberarla.
 */
static char* extract_goes_timestamp(const char* filename) {
    if (!filename) {
        return NULL;
    }

    const char* s_prefix = "_s";
    const char* start_ptr = strstr(filename, s_prefix);

    if (!start_ptr) {
        LOG_DEBUG("Prefijo de firma temporal '_s' no encontrado en: %s", filename);
        return NULL;
    }

    start_ptr += strlen(s_prefix); // Mover el puntero más allá de "_s"

    const int timestamp_len = 11; // YYYYJJJHHMM

    if (strlen(start_ptr) < timestamp_len) {
        LOG_DEBUG("No hay suficientes caracteres para la firma temporal en: %s", filename);
        return NULL;
    }

    char* timestamp = (char*)malloc((timestamp_len + 1) * sizeof(char));
    if (!timestamp) {
        LOG_ERROR("Falla de asignación de memoria para la firma temporal.");
        return NULL;
    }

    strncpy(timestamp, start_ptr, timestamp_len);
    timestamp[timestamp_len] = '\0';

    return timestamp;
}

/**
 * @brief Genera un nombre de archivo de salida por defecto.
 */
char* generate_default_output_filename(const char* input_file_path, const char* processing_mode, const char* output_extension) {
    if (!input_file_path || !processing_mode || !output_extension) {
        return NULL;
    }

    char* timestamp_str = extract_goes_timestamp(input_file_path);
    char* final_output_filename = NULL;
    
    // Si se extrajo la firma, usar el formato nuevo. Si no, usar uno genérico.
    if (timestamp_str) {
        // Longitud: "out" + timestamp + "-" + mode + extension + \0
        size_t required_len = 3 + strlen(timestamp_str) + 1 + strlen(processing_mode) + strlen(output_extension) + 1;
        final_output_filename = (char*)malloc(required_len);
        if (final_output_filename) {
            snprintf(final_output_filename, required_len, "out%s-%s%s", timestamp_str, processing_mode, output_extension);
        }
        free(timestamp_str);
    } else {
        // Fallback al formato antiguo si no se puede extraer la firma
        // Longitud: "out_" + mode + extension + \0
        size_t required_len = 4 + strlen(processing_mode) + strlen(output_extension) + 1;
        final_output_filename = (char*)malloc(required_len);
        if (final_output_filename) {
            snprintf(final_output_filename, required_len, "out_%s%s", processing_mode, output_extension);
        }
    }

    if (!final_output_filename) {
        LOG_ERROR("Falla de asignación de memoria para el nombre de archivo de salida.");
    }

    return final_output_filename;
}

// --- Funciones para expansión de patrones ---

static void julian_to_date(int year, int jday, int *month, int *day) {
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // Leap year check
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
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

    // Extract timestamp: sYYYYJJJHHMMSS
    const char* s_prefix = "_s";
    const char* start_ptr = strstr(input_filename, s_prefix);
    
    // If not found, return copy of pattern
    if (!start_ptr) return strdup(pattern);

    start_ptr += 2; // Skip "_s"
    
    // Parse YYYYJJJHHMMSS (at least 11 chars for YYYYJJJHHMM)
    if (strlen(start_ptr) < 11) return strdup(pattern);

    char s_year[5], s_jday[4], s_hour[3], s_min[3], s_sec[3];
    strncpy(s_year, start_ptr, 4); s_year[4] = 0;
    strncpy(s_jday, start_ptr + 4, 3); s_jday[3] = 0;
    strncpy(s_hour, start_ptr + 7, 2); s_hour[2] = 0;
    strncpy(s_min, start_ptr + 9, 2); s_min[2] = 0;
    
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
    sprintf(s_month, "%02d", month);
    sprintf(s_day, "%02d", day);
    
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
        {"{JJJ}", s_jday},
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