/*
 * HPSatViews - High Performance Satellite Views
 * Main entry point for all processing commands.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "logger.h"
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
    return run_rgb(cmd_parser);
}

int cmd_pseudocolor(char* cmd_name, ArgParser* cmd_parser) {
    return run_processing(cmd_parser, true); // true = is_pseudocolor
}

int cmd_gray(char* cmd_name, ArgParser* cmd_parser) {
    return run_processing(cmd_parser, false); // false = is_pseudocolor
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
    ap_set_version(parser, "3.0");

    // --- Comando 'rgb' ---
    ArgParser *rgb_cmd = ap_new_cmd(parser, "rgb");
    if (rgb_cmd) {
        ap_set_helptext(rgb_cmd, HPSATVIEWS_HELP_RGB);
        ap_add_flag(rgb_cmd, "citylights l");
        ap_add_str_opt(rgb_cmd, "mode m", "daynite");
        add_common_opts(rgb_cmd);
        ap_add_flag(rgb_cmd, "rayleigh");
        ap_add_flag(rgb_cmd, "full-res f");
        ap_set_cmd_callback(rgb_cmd, cmd_rgb);
    }

    // --- Comando 'pseudocolor' ---
    ArgParser *pc_cmd = ap_new_cmd(parser, "pseudocolor");
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

    // Parsear los argumentos UNA SOLA VEZ. La librería ejecutará el callback apropiado.
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
        }

    ap_free(parser);
    return 0;
}
