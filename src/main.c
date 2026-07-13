/* Main entry point: dispatches gray, pseudocolor, and rgb commands.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
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

/// Default path to the clip catalog; overridable via a build-time -D macro.
#define RUTA_CLIPS "/usr/local/share/lanot/docs/recortes_coordenadas.csv"

/// Function pointer type shared by the command runners (run_rgb, run_processing).
typedef int (*ProcessingFunc)(const ProcessConfig *, MetadataContext *);

/// Saves the JSON metadata sidecar when --json was requested.
static void save_sidecar_json(const ProcessConfig *cfg, MetadataContext *meta, ArgParser *parser) {
    if (!ap_found(parser, "json")) {
        return;
    }

    char json_path_buffer[1024];
    const char *final_json_path = NULL;
    char *generated_path = NULL;

    if (cfg->output_path_override) {
        // Use the output path as base; replace extension with .json.
        strncpy(json_path_buffer, cfg->output_path_override, sizeof(json_path_buffer) - 1);
        json_path_buffer[sizeof(json_path_buffer) - 1] = '\0';

        char *last_dot = strrchr(json_path_buffer, '.');
        char *last_slash = strrchr(json_path_buffer, '/');

        // Only strip extension if the dot is after the last directory separator.
        if (last_dot && (!last_slash || last_dot > last_slash)) {
            *last_dot = '\0';
        }
        
        strncat(json_path_buffer, ".json", sizeof(json_path_buffer) - strlen(json_path_buffer) - 1);
        final_json_path = json_path_buffer;
    } else {
        // Auto-generate filename from metadata.
        generated_path = metadata_build_filename(meta, ".json");
        final_json_path = generated_path;
    }

    if (final_json_path) {
        LOG_INFO("Saving metadata to: %s", final_json_path);
        metadata_save_json(meta, final_json_path);
    }
    free(generated_path);
}

/// Shared driver for the gray/pseudocolor/rgb callbacks: validates config, runs the pipeline, and saves the JSON sidecar.
static int generic_cmd_handler(const char *cmd_mode, ArgParser *cmd_parser, ProcessingFunc run_func) {
    ProcessConfig cfg = {0};
    cfg.command = cmd_mode;

    if (!config_from_argparser(cmd_parser, &cfg)) {
        LOG_ERROR("Failed to parse configuration.");
        config_destroy(&cfg);
        return 1;
    }

    if (!config_validate(&cfg)) {
        LOG_ERROR("Invalid configuration.");
        config_destroy(&cfg);
        return 1;
    }

    MetadataContext *meta = metadata_create();
    if (!meta) {
        LOG_ERROR("Failed to create metadata context.");
        config_destroy(&cfg);
        return 1;
    }

    int result = run_func(&cfg, meta);

    if (result == 0) {
        save_sidecar_json(&cfg, meta, cmd_parser);
    }

    metadata_destroy(meta);
    config_destroy(&cfg);

    return result;
}

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

/// Registers the CLI options shared by the gray, pseudocolor, and rgb commands.
static void add_common_opts(ArgParser *cmd_parser) {
    ap_add_str_opt(cmd_parser, "out o", NULL);
    ap_add_flag(cmd_parser, "geotiff t");
    ap_add_str_opt(cmd_parser, "clip c", NULL);
    ap_add_str_opt(cmd_parser, "gamma g", "1.0");
    ap_add_flag(cmd_parser, "histo h");
    ap_add_flag(cmd_parser, "clahe");
    ap_add_str_opt(cmd_parser, "clahe-params", "8,8,4.0");
    ap_add_int_opt(cmd_parser, "scale s", 1);
    ap_add_flag(cmd_parser, "alpha a");
    ap_add_flag(cmd_parser, "geographics G");
    ap_add_flag(cmd_parser, "both B");
    ap_add_flag(cmd_parser, "full-res f");
    ap_add_flag(cmd_parser, "json j");
    ap_add_flag(cmd_parser, "verbose v");
    ap_add_str_opt(cmd_parser, "expr e", NULL);
    ap_add_str_opt(cmd_parser, "minmax", "0.0,255.0");
    ap_add_flag(cmd_parser, "cuda");
}

int main(int argc, char *argv[]) {
    // Pre-scan for global flags that must be resolved before logger_init() and ap_parse().
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

    ArgParser *rgb_cmd = ap_new_cmd(parser, "rgb");
    if (rgb_cmd) {
        ap_set_helptext(rgb_cmd, HPSATVIEWS_HELP_RGB);
        ap_add_flag(rgb_cmd, "citylights l");
        ap_add_str_opt(rgb_cmd, "mode m", "daynite");
        ap_add_str_opt(rgb_cmd, "name N", NULL);
        add_common_opts(rgb_cmd);
        ap_add_flag(rgb_cmd, "rayleigh");
        ap_add_flag(rgb_cmd, "ray-analytic");
        ap_add_flag(rgb_cmd, "stretch");
        ap_add_flag(rgb_cmd, "sharpen");
        ap_add_str_opt(rgb_cmd, "cloud-temp T", "0");
        ap_set_cmd_callback(rgb_cmd, cmd_rgb);
    }

    ArgParser *pc_cmd = ap_new_cmd(parser, "pseudocolor pseudo");
    if (pc_cmd) {
        ap_set_helptext(pc_cmd, HPSATVIEWS_HELP_PSEUDOCOLOR);
        add_common_opts(pc_cmd);
        ap_add_str_opt(pc_cmd, "cpt p", NULL);
        ap_add_flag(pc_cmd, "invert i");
        ap_set_cmd_callback(pc_cmd, cmd_pseudocolor);
    }

    ArgParser *sg_cmd = ap_new_cmd(parser, "gray");
    if (sg_cmd) {
        ap_set_helptext(sg_cmd, HPSATVIEWS_HELP_GRAY);
        add_common_opts(sg_cmd);
        ap_add_flag(sg_cmd, "invert i");
        ap_set_cmd_callback(sg_cmd, cmd_gray);
    }

    if (!ap_parse(parser, argc, argv)) {
        // ap_parse() already prints its own error message on failure.
        ap_free(parser);
        return 1;
    }

    ArgParser *active_cmd = ap_get_cmd_parser(parser);
    if (!active_cmd) {
        // No command was executed (e.g., binary called with no arguments): print version.
        puts(HPSV_VERSION_STRING);
    }

    int exit_code = ap_get_cmd_exit_code(parser);
    ap_free(parser);
    return exit_code;
}
