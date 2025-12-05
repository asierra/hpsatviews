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

// --- Callbacks para los comandos ---
int cmd_rgb(char* cmd_name, ArgParser* cmd_parser) {
    return run_rgb(cmd_parser);
}

int cmd_pseudocolor(char* cmd_name, ArgParser* cmd_parser) {
    return run_processing(cmd_parser, true); // true = is_pseudocolor
}

int cmd_singlegray(char* cmd_name, ArgParser* cmd_parser) {
    return run_processing(cmd_parser, false); // false = is_pseudocolor
}

int main(int argc, char *argv[]) {
    ArgParser *parser = ap_new_parser();
    ap_set_helptext(parser, "Usanza: hpsatviews <comando> [opciones]\n\nComandos:\n"
                            "  rgb          Genera una imagen RGB (ej. color verdadero con composición día/noche).\n"
                            "  pseudocolor  Genera una imagen con paleta de colores a partir de un canal.\n"
                            "  singlegray   Genera una imagen en escala de grises de un solo canal.\n\n"
                            "Use 'hpsatviews help <comando>' para más información sobre un comando específico.");
    ap_set_version(parser, "3.0");

    // --- Comando 'rgb' ---
    ArgParser *rgb_cmd = ap_new_cmd(parser, "rgb");
    if (rgb_cmd) {
        ap_set_helptext(rgb_cmd, "Usanza: hpsatviews rgb [opciones] <Archivo NetCDF de referencia>\n\n"
                                 "Genera una imagen de color verdadero día/noche. Requiere archivos C01, C02, C03, C13 en el mismo directorio.\n\n"
                                 "Opciones:\n"
                                 "  -m, --mode <modo>       Modo de operación. Opciones disponibles:\n"
                                 "                          'composite' (defecto), 'truecolor', 'night', 'ash', 'airmass', 'so2'.\n"
                                 "  -o, --out <archivo>     Archivo de salida (defecto: autogenerado con extensión .png o .tif).\n"
                                 "  -t, --geotiff           Generar salida en formato GeoTIFF (en vez de PNG).\n"
                                 "  -c, --clip <coords>     Recorta la imagen a una ventana geográfica. Formato: lon_min lat_max lon_max lat_min.\n"
                                 "  -g, --gamma <valor>     Corrección gamma a aplicar (defecto: 1.0, sin corrección).\n"
                                 "  -h, --histo             Aplica ecualización de histograma.\n"
                                 "  -s, --scale <factor>    Factor de escala. >1 para ampliar, <0 para reducir (defecto: 1, sin implementar).\n"
                                 "  -a, --alpha             Añade un canal alfa (sin implementar).\n"
                                 "  -r, --geographics       Reproyecta la salida a coordenadas geográficas.\n"
                                 "      --rayleigh          Aplica corrección atmosférica de Rayleigh (solo modos truecolor/composite).");
        ap_add_str_opt(rgb_cmd, "mode m", "composite");
        ap_add_str_opt(rgb_cmd, "out o", NULL);
        ap_add_flag(rgb_cmd, "geotiff t");
        ap_add_multi_str_opt(rgb_cmd, "clip c", 4);
        ap_add_dbl_opt(rgb_cmd, "gamma g", 1.0);
        ap_add_flag(rgb_cmd, "histo h");
        ap_add_int_opt(rgb_cmd, "scale s", 1);
        ap_add_flag(rgb_cmd, "alpha a");
        ap_add_flag(rgb_cmd, "geographics r");
        ap_add_flag(rgb_cmd, "rayleigh");
        ap_add_flag(rgb_cmd, "verbose v");
        ap_set_cmd_callback(rgb_cmd, cmd_rgb);
    }

    // --- Opciones comunes para singlegray y pseudocolor ---
    void add_common_opts(ArgParser* cmd_parser) {
        ap_add_str_opt(cmd_parser, "out o", NULL);
        ap_add_flag(cmd_parser, "geotiff t");
        ap_add_multi_str_opt(cmd_parser, "clip c", 4);
        ap_add_dbl_opt(cmd_parser, "gamma g", 1.0);
        ap_add_flag(cmd_parser, "histo h");
        ap_add_int_opt(cmd_parser, "scale s", 1);
        ap_add_flag(cmd_parser, "geographics r");
        ap_add_flag(cmd_parser, "verbose v");
    }

    // --- Comando 'pseudocolor' ---
    ArgParser *pc_cmd = ap_new_cmd(parser, "pseudocolor");
    if (pc_cmd) {
        ap_set_helptext(pc_cmd, "Usanza: hpsatviews pseudocolor -p <paleta.cpt> [opciones] <Archivo NetCDF>\n\n"
                                "Genera una imagen con paleta de colores (requiere -p/--cpt).\n\n"
                                "Opciones:\n"
                                "  -p, --cpt <archivo>     Aplica una paleta de colores (archivo .cpt). Requerido.\n"
                                "  -o, --out <archivo>     Archivo de salida (defecto: autogenerado con extensión .png o .tif).\n"
                                "  -t, --geotiff           Generar salida en formato GeoTIFF (en vez de PNG).\n"
                                "  -c, --clip <coords>     Recorta la imagen a una ventana geográfica. Formato: lon_min lat_max lon_max lat_min.\n"
                                "  -g, --gamma <valor>     Corrección gamma a aplicar (defecto: 1.0).\n"
                                "  -a, --alpha             Añade un canal alfa (funcionalidad futura).\n"
                                "  -h, --histo             Aplica ecualización de histograma.\n"
                                "  -s, --scale <factor>    Factor de escala. >1 para ampliar, <0 para reducir (defecto: 1).\n"
                                "  -r, --geographics       Reproyecta la salida a coordenadas geográficas.");
        add_common_opts(pc_cmd);
        ap_add_str_opt(pc_cmd, "cpt p", NULL);
        ap_add_flag(pc_cmd, "alpha a");
        ap_set_cmd_callback(pc_cmd, cmd_pseudocolor);
    }

    // --- Comando 'singlegray' ---
    ArgParser *sg_cmd = ap_new_cmd(parser, "singlegray");
    if (sg_cmd) {
        ap_set_helptext(sg_cmd, "Usanza: hpsatviews singlegray [opciones] <Archivo NetCDF>\n\n"
                                "Genera una imagen en escala de grises a partir de una variable NetCDF.\n\n"
                                "Opciones:\n"
                                "  -o, --out <archivo>     Archivo de salida (defecto: autogenerado con extensión .png o .tif).\n"
                                "  -t, --geotiff           Generar salida en formato GeoTIFF (en vez de PNG).\n"
                                "  -c, --clip <coords>     Recorta la imagen a una ventana geográfica. Formato: lon_min lat_max lon_max lat_min.\n"
                                "  -g, --gamma <valor>     Corrección gamma a aplicar (defecto: 1.0).\n"
                                "  -h, --histo             Aplica ecualización de histograma.\n"
                                "  -i, --invert            Invierte los valores (blanco y negro).\n"
                                "  -a, --alpha             Añade un canal alfa.\n"
                                "  -s, --scale <factor>    Factor de escala. >1 para ampliar, <0 para reducir (defecto: 1).\n"
                                "  -r, --geographics       Reproyecta la salida a coordenadas geográficas.");
        add_common_opts(sg_cmd);
        ap_add_flag(sg_cmd, "invert i");
        ap_add_flag(sg_cmd, "alpha a");
        ap_set_cmd_callback(sg_cmd, cmd_singlegray);
    }

    // Parsear los argumentos UNA SOLA VEZ. La librería ejecutará el callback apropiado.
    if (!ap_parse(parser, argc, argv)) {
        // ap_parse ya imprime errores, solo necesitamos salir.
        ap_free(parser);
        return 1;
    }
    
    ArgParser* active_cmd = ap_get_cmd_parser(parser);
    if (active_cmd) {
        LogLevel log_level = ap_found(active_cmd, "verbose") ? LOG_DEBUG : LOG_INFO;
        logger_init(log_level);
        LOG_DEBUG("Logger inicializado en modo verboso.");
    } else {
        // No command was run, maybe just 'help' or 'version'
        logger_init(LOG_INFO);
    }

    ap_free(parser);
    return 0;
}

