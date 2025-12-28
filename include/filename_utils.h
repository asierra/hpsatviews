/*
 * Utilidades para la manipulación de nombres de archivo.
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef FILENAME_UTILS_H_
#define FILENAME_UTILS_H_

#include <stdbool.h>
#include <stddef.h>
#include "datanc.h"

typedef struct {
    const DataNC* datanc;           // Metadatos de tiempo, banda, etc.
    const char* satellite_name;     // "goes-16", "goes-18", etc. (de filename)
    const char* command;            // "gray", "pseudocolor", "rgb"
    const char* mode;               // "truecolor" (RGB) o nombre paleta (Pseudo)
    bool        apply_rayleigh;
    bool        apply_histogram;
    bool        apply_clahe;
    float       gamma;
    bool        has_clip;
    bool        do_reprojection;
    bool        force_geotiff;
    bool        invert_values;
} FilenameGeneratorInfo;

char* generate_hpsv_filename(const FilenameGeneratorInfo* info);

// Helper to extract satellite name from GOES filename (e.g., "goes-16")
char* extract_satellite_name(const char* filename);

char* expand_filename_pattern(const char* pattern, const char* input_filename);

char* generate_default_output_filename(const char* input_file_path, const char* processing_mode, const char* output_extension);

#endif /* FILENAME_UTILS_H_ */