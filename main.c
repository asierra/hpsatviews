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

// Ruta por defecto (se puede definir en un header global o pasar como macro -D)
#define RUTA_CLIPS "/usr/local/share/lanot/docs/recortes_coordenadas.csv"


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

// --- Función helper para opciones comunes ---
static void add_common_opts(ArgParser* cmd_parser) {
    ap_add_str_opt(cmd_parser, "out o", NULL);
    ap_add_flag(cmd_parser, "geotiff t");
    ap_add_str_opt(cmd_parser, "clip c", NULL);
    ap_add_dbl_opt(cmd_parser, "gamma g", 1.0);
    ap_add_flag(cmd_parser, "histo h");
    ap_add_int_opt(cmd_parser, "scale s", 1);
    ap_add_flag(cmd_parser, "alpha a");
    ap_add_flag(cmd_parser, "geographics r");
    ap_add_flag(cmd_parser, "verbose v");
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
    ap_set_helptext(parser, "Usanza: hpsatviews <comando> [opciones]\n\n"
                            "Comandos:\n"
                            "  rgb          Genera una imagen RGB (ej. color verdadero con composición día/noche).\n"
                            "  pseudocolor  Genera una imagen con paleta de colores a partir de un canal.\n"
                            "  singlegray   Genera una imagen en escala de grises de un solo canal.\n\n"
                            "Opciones globales:\n"
                            "  --list-clips            Muestra los recortes geográficos predefinidos disponibles.\n\n"
                            "Opciones comunes (disponibles en todos los comandos):\n"
                            "  -o, --out <archivo>     Archivo de salida (defecto: autogenerado).\n"
                            "                          Soporta patrones con marcadores de tiempo entre llaves:\n"
                            "                          {YYYY}=año, {MM}=mes, {DD}=día, {hh}=hora, {mm}=minuto,\n"
                            "                          {ss}=segundo, {JJJ}=día juliano, {YY}=año de 2 dígitos.\n"
                            "                          Ejemplo: \"imagen_{YYYY}{MM}{DD}_{hh}{mm}.tif\"\n"
                            "  -t, --geotiff           Generar salida en formato GeoTIFF (en vez de PNG).\n"
                            "  -c, --clip <valor>      Recortar imagen. Puede ser una clave (ej. 'mexico') o\n"
                            "                          coordenadas entre comillas: \"lon_min lat_max lon_max lat_min\".\n"
                            "  -g, --gamma <valor>     Corrección gamma (defecto: 1.0).\n"
                            "  -h, --histo             Aplica ecualización de histograma.\n"
                            "  -s, --scale <factor>    Factor de escala. >1 para ampliar, <0 para reducir (defecto: 1).\n"
                            "  -a, --alpha             Añade canal alfa (transparencia en zonas NonData).\n"
                            "  -r, --geographics       Reproyecta la salida a coordenadas geográficas.\n"
                            "  -v, --verbose           Modo verboso (muestra información detallada).\n\n"
                            "Use 'hpsatviews help <comando>' para más información sobre un comando específico.");
    ap_set_version(parser, "3.0");

    // --- Comando 'rgb' ---
    ArgParser *rgb_cmd = ap_new_cmd(parser, "rgb");
    if (rgb_cmd) {
        ap_set_helptext(rgb_cmd, "Usanza: hpsatviews rgb [opciones] <Archivo NetCDF de referencia>\n\n"
                                 "Genera un compuesto RGB. Requiere varios canales en el mismo directorio.\n\n"
                                 "Opciones:\n"
                                 "  -m, --mode <modo>       Modo de operación. Opciones disponibles:\n"
                                 "                          'composite' (defecto), 'truecolor', 'night', 'ash', 'airmass', 'so2'.\n"
                                 "  -o, --out <archivo>     Archivo de salida (defecto: autogenerado con extensión .png o .tif).\n"
                                 "  -t, --geotiff           Generar salida en formato GeoTIFF (en vez de PNG).\n"
                                 "  -c, --clip <valor>      Recorta la imagen a una ventana geográfica.\n"
                                 "                          Puede ser una clave (ej. 'mexico') o 4 coordenadas\n"
                                 "                          entre comillas: \"lon_min lat_max lon_max lat_min\".\n"
                                 "  -g, --gamma <valor>     Corrección gamma a aplicar (defecto: 1.0, sin corrección).\n"
                                 "  -h, --histo             Aplica ecualización de histograma.\n"
                                 "  -s, --scale <factor>    Factor de escala (ver opciones comunes).\n"
                                 "  -a, --alpha             Añade canal alfa (ver opciones comunes).\n"
                                 "  -r, --geographics       Reproyecta la salida a coordenadas geográficas.\n"
                                 "  -v, --verbose           Modo verboso.\n\n"
                                 "Opciones específicas del comando rgb:\n"
                                 "      --rayleigh          Aplica corrección atmosférica de Rayleigh (solo modos truecolor/composite).");
        ap_add_str_opt(rgb_cmd, "mode m", "composite");
        add_common_opts(rgb_cmd);
        ap_add_flag(rgb_cmd, "rayleigh");
        ap_set_cmd_callback(rgb_cmd, cmd_rgb);
    }

    // --- Comando 'pseudocolor' ---
    ArgParser *pc_cmd = ap_new_cmd(parser, "pseudocolor");
    if (pc_cmd) {
        ap_set_helptext(pc_cmd, "Usanza: hpsatviews pseudocolor -p <paleta.cpt> [opciones] <Archivo NetCDF>\n\n"
                                "Genera una imagen con paleta de colores a partir de un canal NetCDF.\n\n"
                                "Opciones específicas del comando pseudocolor:\n"
                                "  -p, --cpt <archivo>     Aplica una paleta de colores (archivo .cpt). Requerido.\n"
                                "  -i, --invert            Invierte los valores (blanco y negro).\n\n"
                                "Para opciones comunes (out, clip, gamma, histo, scale, alpha, etc.), use 'hpsatviews --help'.");
        add_common_opts(pc_cmd);
        ap_add_str_opt(pc_cmd, "cpt p", NULL);
        ap_add_flag(pc_cmd, "invert i");
        ap_set_cmd_callback(pc_cmd, cmd_pseudocolor);
    }

    // --- Comando 'singlegray' ---
    ArgParser *sg_cmd = ap_new_cmd(parser, "singlegray");
    if (sg_cmd) {
        ap_set_helptext(sg_cmd, "Usanza: hpsatviews singlegray [opciones] <Archivo NetCDF>\n\n"
                                "Genera una imagen en escala de grises a partir de una variable NetCDF.\n\n"
                                "Opciones específicas del comando singlegray:\n"
                                "  -i, --invert            Invierte los valores (blanco y negro).\n\n"
                                "Para opciones comunes (out, clip, gamma, histo, scale, alpha, etc.), use 'hpsatviews --help'.");
        add_common_opts(sg_cmd);
        ap_add_flag(sg_cmd, "invert i");
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

