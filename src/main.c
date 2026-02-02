/*
 * HPSatViews - High Performance Satellite Views
 * Main entry point for all processing commands.
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "clip_loader.h"
#include "config.h"
#include "logger.h"
#include "metadata.h"
#include "processing.h"
#include "rgb.h"
#include "version.h"

#ifdef HPSV_LANG_ES
#include "help_es.h"
#else
#include "help_en.h"
#endif

// Ruta por defecto (se puede definir en un header global o pasar como macro -D)
#define RUTA_CLIPS "/usr/local/share/lanot/docs/recortes_coordenadas.csv"

// Tipo de función para los runners de procesamiento (run_rgb, run_processing)
typedef int (*ProcessingFunc)(const ProcessConfig *, MetadataContext *);

// --- Helper: Guardar JSON sidecar ---
static void save_sidecar_json(const ProcessConfig *cfg, MetadataContext *meta, ArgParser *parser) {
    if (!ap_found(parser, "json")) {
        return;
    }

    char json_path_buffer[1024];
    const char *final_json_path = NULL;
    char *generated_path = NULL;

    if (cfg->output_path_override) {
        // Usar la ruta de salida como base, reemplazando la extensión
        strncpy(json_path_buffer, cfg->output_path_override, sizeof(json_path_buffer) - 1);
        json_path_buffer[sizeof(json_path_buffer) - 1] = '\0';

        char *last_dot = strrchr(json_path_buffer, '.');
        char *last_slash = strrchr(json_path_buffer, '/');

        // Solo quitar extensión si el punto está después del último separador de directorios
        if (last_dot && (!last_slash || last_dot > last_slash)) {
            *last_dot = '\0';
        }
        
        // Concatenar .json de forma segura
        strncat(json_path_buffer, ".json", sizeof(json_path_buffer) - strlen(json_path_buffer) - 1);
        final_json_path = json_path_buffer;
    } else {
        // Generar nombre automático desde metadatos
        generated_path = metadata_build_filename(meta, ".json");
        final_json_path = generated_path;
    }

    if (final_json_path) {
        LOG_INFO("Guardando metadatos en: %s", final_json_path);
        metadata_save_json(meta, final_json_path);
    }
    free(generated_path);
}

// --- Helper: Manejador genérico de comandos ---
static int generic_cmd_handler(const char *cmd_mode, ArgParser *cmd_parser, ProcessingFunc run_func) {
    ProcessConfig cfg = {0};
    cfg.command = cmd_mode;

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

    // Ejecutar la función de procesamiento específica (run_rgb o run_processing)
    int result = run_func(&cfg, meta);

    if (result == 0) {
        save_sidecar_json(&cfg, meta, cmd_parser);
    }

    metadata_destroy(meta);
    config_destroy(&cfg);

    return result;
}

// --- Callbacks para los comandos ---

int cmd_rgb(char *cmd_name, ArgParser *cmd_parser) {
    (void)cmd_name;
    return generic_cmd_handler("rgb", cmd_parser, run_rgb);
}

int cmd_pseudocolor(char *cmd_name, ArgParser *cmd_parser) {
    (void)cmd_name;
    return generic_cmd_handler("pseudocolor", cmd_parser, run_processing);
}

int cmd_gray(char *cmd_name, ArgParser *cmd_parser) {
    (void)cmd_name;
    return generic_cmd_handler("gray", cmd_parser, run_processing);
}

// --- Función helper para opciones comunes ---
static void add_common_opts(ArgParser *cmd_parser) {
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
    ap_add_flag(cmd_parser, "json j");
    ap_add_flag(cmd_parser, "verbose v");
    ap_add_str_opt(cmd_parser, "expr e", NULL);
    ap_add_str_opt(cmd_parser, "minmax", "0.0,255.0");
}

int main(int argc, char *argv[]) {
    // Verificar argumentos globales antes de parsear
    bool verbose_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-clips") == 0) {
            printf("Recortes geográficos disponibles:\n\n");
            listar_clips_disponibles(RUTA_CLIPS);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_mode = true;
        }
    }

    ArgParser *parser = ap_new_parser();
    ap_set_helptext(parser, HPSATVIEWS_HELP);
    ap_set_version(parser, HPSV_VERSION_STRING);

#ifdef DEBUG_MODE
    logger_init(LOG_DEBUG);
#else
    logger_init(verbose_mode ? LOG_DEBUG : LOG_INFO);
#endif

    // --- Comando 'rgb' ---
    ArgParser *rgb_cmd = ap_new_cmd(parser, "rgb");
    if (rgb_cmd) {
        ap_set_helptext(rgb_cmd, HPSATVIEWS_HELP_RGB);
        ap_add_flag(rgb_cmd, "citylights l");
        ap_add_str_opt(rgb_cmd, "mode m", "daynite");
        add_common_opts(rgb_cmd);
        ap_add_flag(rgb_cmd, "rayleigh");
        ap_add_flag(rgb_cmd, "ray-analytic");
        ap_add_flag(rgb_cmd, "stretch");
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

    ArgParser *active_cmd = ap_get_cmd_parser(parser);
    if (!active_cmd) {
        // Si no se ejecutó ningún comando (ej. solo se llamó al binario), mostrar versión
        puts(HPSV_VERSION_STRING);
    }

    ap_free(parser);
    return 0;
}
