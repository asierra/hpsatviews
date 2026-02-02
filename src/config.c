/*
 * Configuration Module - Centraliza el parseo de argumentos
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "config.h"
#include "args.h"
#include "clip_loader.h"
#include "logger.h"
#include "filename_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>

/**
 * Procesa la opción --clip, que puede ser:
 * 1. Cuatro coordenadas: "lon_min,lat_max,lon_max,lat_min"
 * 2. Una clave del CSV: "mexico", "conus", etc.
 * 
 * @param parser ArgParser con los argumentos parseados
 * @param cfg ProcessConfig donde se guardarán las coordenadas
 * @return true si se encontró y procesó un clip válido
 */
static bool config_parse_clip(ArgParser* parser, ProcessConfig* cfg) {
    if (!ap_found(parser, "clip")) {
        return false;
    }
    
    const char* clip_value = ap_get_str_value(parser, "clip");
    if (!clip_value || strlen(clip_value) == 0) {
        return false;
    }
    
    // Intentar parsear como 4 coordenadas
    float coords[4];
    int parsed = sscanf(clip_value, "%f%*[, ]%f%*[, ]%f%*[, ]%f", 
                       &coords[0], &coords[1], &coords[2], &coords[3]);
    
    if (parsed == 4) {
        // Coordenadas explícitas
        for (int i = 0; i < 4; i++) {
            cfg->clip_coords[i] = coords[i];
        }
        cfg->has_clip = true;
        LOG_INFO("Clip con coordenadas: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                 cfg->clip_coords[0], cfg->clip_coords[2], 
                 cfg->clip_coords[3], cfg->clip_coords[1]);
        return true;
    }
    
    // Intentar cargar desde CSV
    const char* clip_csv = "/usr/local/share/lanot/docs/recortes_coordenadas.csv";
    GeoClip clip = buscar_clip_por_clave(clip_csv, clip_value);
    
    if (!clip.encontrado) {
        LOG_WARN("No se encontró el recorte '%s' en %s", clip_value, clip_csv);
        return false;
    }
    
    LOG_INFO("Usando recorte '%s': %s", clip_value, clip.region);
    cfg->clip_coords[0] = clip.ul_x;  // lon_min
    cfg->clip_coords[1] = clip.ul_y;  // lat_max
    cfg->clip_coords[2] = clip.lr_x;  // lon_max
    cfg->clip_coords[3] = clip.lr_y;  // lat_min
    cfg->has_clip = true;
    return true;
}

/**
 * Procesa parámetros de CLAHE: --clahe o --clahe-params=x,y,limit
 * 
 * @param parser ArgParser con los argumentos
 * @param cfg ProcessConfig donde se guardarán los parámetros
 */
static void config_parse_clahe(ArgParser* parser, ProcessConfig* cfg) {
    bool has_clahe_flag = ap_found(parser, "clahe");
    bool has_clahe_params = ap_found(parser, "clahe-params");
    
    cfg->apply_clahe = has_clahe_flag || has_clahe_params;
    
    if (!cfg->apply_clahe) {
        return;
    }
    
    // Valores por defecto
    cfg->clahe_tiles_x = 8;
    cfg->clahe_tiles_y = 8;
    cfg->clahe_clip_limit = 4.0f;
    
    if (has_clahe_params) {
        const char* params = ap_get_str_value(parser, "clahe-params");
        if (params) {
            int parsed = sscanf(params, "%d,%d,%f", 
                              &cfg->clahe_tiles_x, 
                              &cfg->clahe_tiles_y, 
                              &cfg->clahe_clip_limit);
            // Si faltan parámetros, mantener defaults para los faltantes
            if (parsed < 1) cfg->clahe_tiles_x = 8;
            if (parsed < 2) cfg->clahe_tiles_y = 8;
            if (parsed < 3) cfg->clahe_clip_limit = 4.0f;
            
            LOG_DEBUG("CLAHE params: tiles=%dx%d, clip_limit=%.2f", 
                     cfg->clahe_tiles_x, cfg->clahe_tiles_y, cfg->clahe_clip_limit);
        }
    }
}

/**
 * Procesa la opción --out, expandiendo patrones si es necesario
 * 
 * @param parser ArgParser con los argumentos
 * @param input_file Archivo de entrada (para expansión de patrones)
 * @return String dinámico con el path de salida (NULL si no se especificó)
 *         El caller debe liberar la memoria si no es NULL
 */
static char* config_parse_output(ArgParser* parser, const char* input_file) {
    if (!ap_found(parser, "out")) {
        return NULL;
    }
    
    const char* user_out = ap_get_str_value(parser, "out");
    if (!user_out) {
        return NULL;
    }
    
    // Detectar patrones con llaves y expandirlos
    if (strchr(user_out, '{') && strchr(user_out, '}')) {
        return expand_filename_pattern(user_out, input_file);
    }
    
    // No hay patrón, devolver copia del string original
    return strdup(user_out);
}

bool config_from_argparser(ArgParser* parser, ProcessConfig* cfg) {
    if (!parser || !cfg) {
        LOG_ERROR("config_from_argparser: parámetros NULL");
        return false;
    }
    
    // --- Archivo de entrada (requerido) ---
    if (!ap_has_args(parser)) {
        LOG_ERROR("Se requiere un archivo NetCDF de entrada.");
        return false;
    }
    cfg->input_file = ap_get_arg_at_index(parser, 0);
    
    // --- Comando y estrategia ---
    // Estos se deben establecer externamente antes de llamar a esta función
    // (desde main.c en el dispatcher de comandos)
    // Si no están establecidos, usar valores por defecto
    if (!cfg->command) {
        cfg->command = "unknown";
    }
    
    // Modo/estrategia (para RGB principalmente)
    // Solo procesar si el comando es 'rgb' (único que tiene opción "mode")
    if (cfg->command && strcmp(cfg->command, "rgb") == 0) {
        if (ap_found(parser, "mode")) {
            const char* mode = ap_get_str_value(parser, "mode");
            if (mode) {
                cfg->strategy = mode;
            }
        }
    }
    if (!cfg->strategy) {
        cfg->strategy = "default";
    }
    
    // --- Parámetros Físicos y Realce ---
    
    // Gamma (default: 1.0)
    cfg->gamma = 1.0f;
    if (ap_found(parser, "gamma")) {
        double gamma_val = ap_get_dbl_value(parser, "gamma");
        if (gamma_val > 0.0) {
            cfg->gamma = (float)gamma_val;
        }
    }
    
    // CLAHE
    config_parse_clahe(parser, cfg);
    
    // Ecualización de histograma
    cfg->apply_histogram = ap_found(parser, "histo");
    
    // Corrección Rayleigh (solo para RGB)
    if (cfg->command && strcmp(cfg->command, "rgb") == 0) {
        cfg->apply_rayleigh = ap_found(parser, "rayleigh");
        cfg->rayleigh_analytic = ap_found(parser, "ray-analytic");
        cfg->use_piecewise_stretch = ap_found(parser, "stretch");
    }
    
    // Inversión de valores (solo para gray/pseudocolor)
    bool is_processing_cmd = (cfg->command && 
                             (strcmp(cfg->command, "gray") == 0 || 
                              strcmp(cfg->command, "pseudocolor") == 0));
    if (is_processing_cmd) {
        cfg->invert_values = ap_found(parser, "invert");
    }
    
    // --- Opciones de Composición ---
    
    // Scale
    cfg->scale = 1;
    if (ap_found(parser, "scale")) {
        cfg->scale = ap_get_int_value(parser, "scale");
        if (cfg->scale == 0) cfg->scale = 1;
    }
    
    // Opciones específicas de RGB
    bool is_rgb = (cfg->command && strcmp(cfg->command, "rgb") == 0);
    if (is_rgb) {
        // Canal alfa (transparencia)
        cfg->use_alpha = ap_found(parser, "alpha");
        
        // Luces de ciudad (fondo nocturno)
        cfg->use_citylights = ap_found(parser, "citylights");
        
        // Full resolution (para productos L2 que tienen baja resolución)
        cfg->use_full_res = ap_found(parser, "full-res");
        
        // --- Álgebra de Bandas (Custom Mode) ---
        cfg->is_custom_mode = ap_found(parser, "expr");
        
        if (cfg->is_custom_mode) {
            cfg->custom_expr = ap_get_str_value(parser, "expr");
            cfg->custom_minmax = ap_get_str_value(parser, "minmax");
        }
    }
    
    // --- Pseudocolor (Paletas CPT) ---
    // Solo para comando pseudocolor
    bool is_pseudocolor = (cfg->command && strcmp(cfg->command, "pseudocolor") == 0);
    if (is_pseudocolor && ap_found(parser, "cpt")) {
        cfg->palette_file = ap_get_str_value(parser, "cpt");
    }
    
    // --- Geometría ---
    config_parse_clip(parser, cfg);
    cfg->do_reprojection = ap_found(parser, "geographics");
    
    // --- Salida ---
    cfg->force_geotiff = ap_found(parser, "geotiff");
    cfg->output_path_override = config_parse_output(parser, cfg->input_file);
    
    // Si se fuerza GeoTIFF y el output tiene extensión .png, cambiarla
    if (cfg->force_geotiff && cfg->output_path_override) {
        const char *ext = strrchr(cfg->output_path_override, '.');
        if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0)) {
            size_t base_len = ext - cfg->output_path_override;
            char* new_path = malloc(base_len + 5); // ".tif\0"
            if (new_path) {
                strncpy(new_path, cfg->output_path_override, base_len);
                strcpy(new_path + base_len, ".tif");
                LOG_INFO("Extensión cambiada de .png a .tif: %s", new_path);
                free((void*)cfg->output_path_override);
                cfg->output_path_override = new_path;
            }
        }
    }
    
    // Detectar si es producto L2 (CMIP en el nombre)
    cfg->is_l2_product = false;
    if (cfg->input_file) {
        char* dup = strdup(cfg->input_file);
        if (dup) {
            const char* base = basename(dup);
            cfg->is_l2_product = (strstr(base, "CMIP") != NULL);
            free(dup);
        }
    }
    
    return true;
}

bool config_validate(const ProcessConfig* cfg) {
    if (!cfg) {
        return false;
    }
    
    // Validar archivo de entrada
    if (!cfg->input_file || strlen(cfg->input_file) == 0) {
        LOG_ERROR("Archivo de entrada requerido");
        return false;
    }
    
    // Validar gamma
    if (cfg->gamma <= 0.0f || cfg->gamma > 5.0f) {
        LOG_ERROR("Gamma debe estar en el rango (0.0, 5.0], valor: %.2f", cfg->gamma);
        return false;
    }
    
    // Validar CLAHE
    if (cfg->apply_clahe) {
        if (cfg->clahe_tiles_x < 2 || cfg->clahe_tiles_x > 64) {
            LOG_ERROR("clahe_tiles_x debe estar en [2, 64], valor: %d", cfg->clahe_tiles_x);
            return false;
        }
        if (cfg->clahe_tiles_y < 2 || cfg->clahe_tiles_y > 64) {
            LOG_ERROR("clahe_tiles_y debe estar en [2, 64], valor: %d", cfg->clahe_tiles_y);
            return false;
        }
        if (cfg->clahe_clip_limit <= 0.0f || cfg->clahe_clip_limit > 100.0f) {
            LOG_ERROR("clahe_clip_limit debe estar en (0.0, 100.0], valor: %.2f", 
                     cfg->clahe_clip_limit);
            return false;
        }
    }
    
    // Validar scale
    if (cfg->scale == 0 || cfg->scale > 10 || cfg->scale < -10) {
        LOG_ERROR("scale debe estar en [-10, -1] o [1, 10], valor: %d", cfg->scale);
        return false;
    }
    
    // Validar clip (si está presente)
    if (cfg->has_clip) {
        float lon_min = cfg->clip_coords[0];
        float lat_max = cfg->clip_coords[1];
        float lon_max = cfg->clip_coords[2];
        float lat_min = cfg->clip_coords[3];
        
        if (lon_min >= lon_max) {
            LOG_ERROR("Clip inválido: lon_min (%.2f) >= lon_max (%.2f)", lon_min, lon_max);
            return false;
        }
        if (lat_min >= lat_max) {
            LOG_ERROR("Clip inválido: lat_min (%.2f) >= lat_max (%.2f)", lat_min, lat_max);
            return false;
        }
        if (lon_min < -180.0f || lon_max > 180.0f) {
            LOG_ERROR("Longitudes fuera del rango válido [-180, 180]");
            return false;
        }
        if (lat_min < -90.0f || lat_max > 90.0f) {
            LOG_ERROR("Latitudes fuera del rango válido [-90, 90]");
            return false;
        }
    }
    
    // Advertencias (no son errores fatales)
    if (cfg->apply_rayleigh && cfg->rayleigh_analytic) {
        LOG_WARN("Se especificaron --rayleigh y --ray-analytic. "
                "Se usará el método analítico.");
    }
    
    return true;
}

void config_print_debug(const ProcessConfig* cfg) {
    if (!cfg) {
        LOG_DEBUG("config_print_debug: cfg es NULL");
        return;
    }
    
    LOG_DEBUG("=== ProcessConfig ===");
    LOG_DEBUG("  command: %s", cfg->command ? cfg->command : "NULL");
    LOG_DEBUG("  strategy: %s", cfg->strategy ? cfg->strategy : "NULL");
    LOG_DEBUG("  input_file: %s", cfg->input_file ? cfg->input_file : "NULL");
    LOG_DEBUG("  is_l2_product: %s", cfg->is_l2_product ? "true" : "false");
    
    LOG_DEBUG("--- Realce ---");
    LOG_DEBUG("  gamma: %.2f", cfg->gamma);
    LOG_DEBUG("  apply_clahe: %s", cfg->apply_clahe ? "true" : "false");
    if (cfg->apply_clahe) {
        LOG_DEBUG("    tiles: %dx%d, clip_limit: %.2f", 
                 cfg->clahe_tiles_x, cfg->clahe_tiles_y, cfg->clahe_clip_limit);
    }
    LOG_DEBUG("  apply_histogram: %s", cfg->apply_histogram ? "true" : "false");
    LOG_DEBUG("  apply_rayleigh: %s", cfg->apply_rayleigh ? "true" : "false");
    LOG_DEBUG("  rayleigh_analytic: %s", cfg->rayleigh_analytic ? "true" : "false");
    LOG_DEBUG("  use_piecewise_stretch: %s", cfg->use_piecewise_stretch ? "true" : "false");
    LOG_DEBUG("  invert_values: %s", cfg->invert_values ? "true" : "false");
    
    LOG_DEBUG("--- Composición ---");
    LOG_DEBUG("  scale: %d", cfg->scale);
    LOG_DEBUG("  use_alpha: %s", cfg->use_alpha ? "true" : "false");
    LOG_DEBUG("  use_citylights: %s", cfg->use_citylights ? "true" : "false");
    LOG_DEBUG("  use_full_res: %s", cfg->use_full_res ? "true" : "false");
    
    LOG_DEBUG("--- Custom Mode ---");
    LOG_DEBUG("  is_custom_mode: %s", cfg->is_custom_mode ? "true" : "false");
    if (cfg->is_custom_mode) {
        LOG_DEBUG("    expr: %s", cfg->custom_expr ? cfg->custom_expr : "NULL");
        LOG_DEBUG("    minmax: %s", cfg->custom_minmax ? cfg->custom_minmax : "NULL");
    }
    
    LOG_DEBUG("--- Pseudocolor ---");
    LOG_DEBUG("  palette_file: %s", cfg->palette_file ? cfg->palette_file : "NULL");
    
    LOG_DEBUG("--- Geometría ---");
    LOG_DEBUG("  has_clip: %s", cfg->has_clip ? "true" : "false");
    if (cfg->has_clip) {
        LOG_DEBUG("    coords: [%.3f, %.3f, %.3f, %.3f]", 
                 cfg->clip_coords[0], cfg->clip_coords[1], 
                 cfg->clip_coords[2], cfg->clip_coords[3]);
    }
    LOG_DEBUG("  do_reprojection: %s", cfg->do_reprojection ? "true" : "false");
    
    LOG_DEBUG("--- Salida ---");
    LOG_DEBUG("  force_geotiff: %s", cfg->force_geotiff ? "true" : "false");
    LOG_DEBUG("  output_override: %s", 
             cfg->output_path_override ? cfg->output_path_override : "NULL");
    LOG_DEBUG("=====================");
}

void config_destroy(ProcessConfig* cfg) {
    if (!cfg) {
        return;
    }
    
    // Solo output_path_override es dinámico (fue creado con strdup/malloc)
    if (cfg->output_path_override) {
        free((void*)cfg->output_path_override);
        cfg->output_path_override = NULL;
    }
    
    // Los demás campos son punteros a strings del ArgParser
    // que se liberarán cuando se destruya el parser
}
