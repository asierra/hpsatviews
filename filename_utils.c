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