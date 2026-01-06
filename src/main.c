/*
 * HPSatViews - High Performance Satellite Views
 * Main entry point for all processing commands.
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"
#include "args.h"
#include "config.h"
#include "logger.h"
#include "metadata.h"
#include "rgb.h"
#include "processing.h"
#include "clip_loader.h"

#ifdef HPSV_LANG_ES
  #include "help_es.h"
#else
  #include "help_en.h"
#endif

// Ruta por defecto (se puede definir en un header global o pasar como macro -D)
#define RUTA_CLIPS "/usr/local/share/lanot/docs/recortes_coordenadas.csv"


// --- Callbacks para los comandos ---
int cmd_rgb(char* cmd_name, ArgParser* cmd_parser) {
    (void)cmd_name;
    
    ProcessConfig cfg = {0};
    cfg.command = "rgb";
    
    if (!config_from_argparser(cmd_parser, &cfg)) {
        LOG_ERROR("Error al parsear configuración");
        config_destroy(&cfg);
        return 1;
    }
    
    if (!config_validate(&cfg)) {
        LOG_ERROR("Configuración inválida");
        config_destroy(&cfg);
        return 1;
    }
    
    MetadataContext *meta = metadata_create();
    if (!meta) {
        LOG_ERROR("Error al crear contexto de metadatos");
        config_destroy(&cfg);
        return 1;
    }
    
    int result = run_rgb(&cfg, meta);
    
    // Guardar JSON sidecar si se especificó salida
    if (result == 0 && cfg.output_path_override) {
        char json_path[512];
        snprintf(json_path, sizeof(json_path), "%s", cfg.output_path_override);
        
        // Cambiar extensión a .json
        char *ext = strrchr(json_path, '.');
        if (ext) *ext = '\0';
        strncat(json_path, ".json", sizeof(json_path) - strlen(json_path) - 1);
        
        LOG_INFO("Guardando metadatos en: %s", json_path);
        metadata_save_json(meta, json_path);
    }
    
    metadata_destroy(meta);
    config_destroy(&cfg);
    
    return result;
}

int cmd_pseudocolor(char* cmd_name, ArgParser* cmd_parser) {
    (void)cmd_name;
    
    ProcessConfig cfg = {0};
    cfg.command = "pseudocolor";
    
    if (!config_from_argparser(cmd_parser, &cfg)) {
        LOG_ERROR("Error al parsear configuración");
        config_destroy(&cfg);
        return 1;
    }
    
    if (!config_validate(&cfg)) {
        LOG_ERROR("Configuración inválida");
        config_destroy(&cfg);
        return 1;
    }
    
    MetadataContext *meta = metadata_create();
    if (!meta) {
        LOG_ERROR("Error al crear contexto de metadatos");
        config_destroy(&cfg);
        return 1;
    }
    
    int result = run_processing(&cfg, meta);
    
    // Guardar JSON sidecar si se especificó salida
    if (result == 0 && cfg.output_path_override) {
        char json_path[512];
        snprintf(json_path, sizeof(json_path), "%s", cfg.output_path_override);
        
        char *ext = strrchr(json_path, '.');
        if (ext) *ext = '\0';
        strncat(json_path, ".json", sizeof(json_path) - strlen(json_path) - 1);
        
        LOG_INFO("Guardando metadatos en: %s", json_path);
        metadata_save_json(meta, json_path);
    }
    
    metadata_destroy(meta);
    config_destroy(&cfg);
    
    return result;
}

int cmd_gray(char* cmd_name, ArgParser* cmd_parser) {
    (void)cmd_name;
    
    ProcessConfig cfg = {0};
    cfg.command = "gray";
    
    if (!config_from_argparser(cmd_parser, &cfg)) {
        LOG_ERROR("Error al parsear configuración");
        config_destroy(&cfg);
        return 1;
    }
    
    if (!config_validate(&cfg)) {
        LOG_ERROR("Configuración inválida");
        config_destroy(&cfg);
        return 1;
    }
    
    MetadataContext *meta = metadata_create();
    if (!meta) {
        LOG_ERROR("Error al crear contexto de metadatos");
        config_destroy(&cfg);
        return 1;
    }
    
    int result = run_processing(&cfg, meta);
    
    // Guardar JSON sidecar si se especificó salida
    if (result == 0 && cfg.output_path_override) {
        char json_path[512];
        snprintf(json_path, sizeof(json_path), "%s", cfg.output_path_override);
        
        char *ext = strrchr(json_path, '.');
        if (ext) *ext = '\0';
        strncat(json_path, ".json", sizeof(json_path) - strlen(json_path) - 1);
        
        LOG_INFO("Guardando metadatos en: %s", json_path);
        metadata_save_json(meta, json_path);
    }
    
    metadata_destroy(meta);
    config_destroy(&cfg);
    
    return result;
}

// --- Función helper para opciones comunes ---
static void add_common_opts(ArgParser* cmd_parser) {
    ap_add_str_opt(cmd_parser, "out o", NULL);
    ap_add_flag(cmd_parser, "geotiff t");
    ap_add_str_opt(cmd_parser, "clip c", NULL);
    ap_add_dbl_opt(cmd_parser, "gamma g", 1.0);
    ap_add_flag(cmd_parser, "histo h");
    ap_add_flag(cmd_parser, "clahe");
    ap_add_str_opt(cmd_parser, "clahe-params", "8,8,4.0");
    ap_add_int_opt(cmd_parser, "scale s", 1);
    ap_add_flag(cmd_parser, "alpha a");
    ap_add_flag(cmd_parser, "geographics r");
    ap_add_flag(cmd_parser, "full-res f");
    ap_add_flag(cmd_parser, "verbose v");
    ap_add_str_opt(cmd_parser, "expr e", NULL);
    ap_add_str_opt(cmd_parser, "minmax", "0.0,255.0");
}


int main(int argc, char *argv[]) {
    // Verificar --list-clips antes de parsear (para salir rápido)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-clips") == 0) {
            printf("Recortes geográficos disponibles:\n\n");
            listar_clips_disponibles(RUTA_CLIPS);
            return 0;
        }
    }
    
    ArgParser *parser = ap_new_parser();
    ap_set_helptext(parser, HPSATVIEWS_HELP);
    ap_set_version(parser, HPSV_VERSION_STRING);

    // --- Comando 'rgb' ---
    ArgParser *rgb_cmd = ap_new_cmd(parser, "rgb");
    if (rgb_cmd) {
        ap_set_helptext(rgb_cmd, HPSATVIEWS_HELP_RGB);
        ap_add_flag(rgb_cmd, "citylights l");
        ap_add_str_opt(rgb_cmd, "mode m", "daynite");
        add_common_opts(rgb_cmd);
        ap_add_flag(rgb_cmd, "rayleigh");
        ap_add_flag(rgb_cmd, "ray-analytic");
        ap_add_flag(rgb_cmd, "full-res f");
        ap_set_cmd_callback(rgb_cmd, cmd_rgb);
    }

    // --- Comando 'pseudocolor' ---
    ArgParser *pc_cmd = ap_new_cmd(parser, "pseudocolor pseudo");
    if (pc_cmd) {
        ap_set_helptext(pc_cmd, HPSATVIEWS_HELP_PSEUDOCOLOR);
        add_common_opts(pc_cmd);
        ap_add_str_opt(pc_cmd, "cpt p", NULL);
        ap_add_flag(pc_cmd, "invert i");
        ap_set_cmd_callback(pc_cmd, cmd_pseudocolor);
    }

    // --- Comando 'gray' ---
    ArgParser *sg_cmd = ap_new_cmd(parser, "gray");
    if (sg_cmd) {
        ap_set_helptext(sg_cmd, HPSATVIEWS_HELP_GRAY);
        add_common_opts(sg_cmd);
        ap_add_flag(sg_cmd, "invert i");
        ap_set_cmd_callback(sg_cmd, cmd_gray);
    }

    if (!ap_parse(parser, argc, argv)) {
        // ap_parse ya imprime errores, solo necesitamos salir.
        ap_free(parser);
        return 1;
    }
    
    ArgParser* active_cmd = ap_get_cmd_parser(parser);
        if (active_cmd) {
    #ifdef DEBUG_MODE
        LogLevel log_level = LOG_DEBUG;
    #else
        LogLevel log_level = ap_found(active_cmd, "verbose") ? LOG_DEBUG : LOG_INFO;
    #endif
        logger_init(log_level);
        LOG_DEBUG("Logger inicializado en modo %s.",
    #ifdef DEBUG_MODE
            "debug (compilación)"
    #else
            ap_found(active_cmd, "verbose") ? "verboso" : "normal"
    #endif
        );
        } else {
        // No command was run, maybe just 'help' or 'version'
    #ifdef DEBUG_MODE
        logger_init(LOG_DEBUG);
    #else
        logger_init(LOG_INFO);
    #endif
		puts(HPSV_VERSION_STRING);
        }

    ap_free(parser);
    return 0;
}
