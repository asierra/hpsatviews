/*
 * RGB and day/night composite generation module.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_RGB_H_
#define HPSATVIEWS_RGB_H_

#include <stdbool.h>
#include "channelset.h"
#include "datanc.h"
#include "image.h"

// Forward declarations
typedef struct ArgParser ArgParser;

/**
 * @brief Contiene todas las opciones de configuración parseadas desde la línea de comandos.
 * Esta estructura es de solo lectura después del parsing inicial.
 */
typedef struct {
    // Identificación
    const char *input_file;        // Archivo NetCDF de referencia
    const char *mode;              // "truecolor", "ash", "airmass", "night", "so2", "composite"
    char *output_filename;         // NULL = generado automáticamente
    bool output_generated;         // true si output_filename fue malloc'd
    
    // Reproyección y recorte
    bool do_reprojection;          // --geographics
    bool has_clip;                 // --clip presente
    float clip_coords[4];          // [lon_min, lat_max, lon_max, lat_min]
    
    // Post-procesamiento
    float gamma;                   // --gamma (default=1.0)
    bool apply_histogram;          // --histo
    bool apply_clahe;              // --clahe o --clahe-params
    int clahe_tiles_x;             // default=8
    int clahe_tiles_y;             // default=8
    float clahe_clip_limit;        // default=4.0
    int scale;                     // --scale (negativo=down, positivo=up, 1=sin cambio)
    
    // Opciones especiales
    bool apply_rayleigh;           // --rayleigh (solo truecolor/composite)
    bool use_citylights;           // --citylights (solo night/composite)
    bool use_alpha;                // --alpha
    bool force_geotiff;            // --geotiff
    bool use_full_res;             // --full-res
    
    // Interno
    bool is_l2_product;            // Detectado automáticamente (contiene "CMIP")
} RgbOptions;

/**
 * @brief Contenedor de estado para toda la operación RGB.
 * Mantiene todos los recursos (datos, configuración, resultados) en un solo lugar.
 */
typedef struct {
    // Configuración
    RgbOptions opts;

    // Gestión de Archivos
    ChannelSet *channel_set;       // Contiene filenames de canales necesarios
    char id_signature[40];         // Extraído del input_file
    
    // Datos de Canales (índices 1-16, [0] sin usar)
    DataNC channels[17];
    int ref_channel_idx;           // Canal con mayor resolución cargado
    
    // Navegación
    DataF nav_lat;
    DataF nav_lon;
    bool has_navigation;
    
    // Reproyección (solo si do_reprojection=true)
    float final_lon_min, final_lon_max;
    float final_lat_min, final_lat_max;
    unsigned crop_x_offset, crop_y_offset; // Para GeoTIFF nativo con clip
    
    // Resultado final
    ImageData final_image;
    ImageData alpha_mask;

    // Estado de error
    bool error_occurred;
    char error_msg[512];
} RgbContext;

// Declaración del tipo de función para compositores RGB
typedef ImageData (*RgbComposer)(RgbContext *ctx);

/**
 * @brief Define una estrategia RGB (modo de composición).
 */
typedef struct {
    const char *mode_name;           // "ash", "truecolor", etc
    const char *req_channels[6];     // {"C11", "C13", "C14", "C15", NULL}
    RgbComposer composer_func;       // Puntero a función
    const char *description;         // Para help/documentación
    bool needs_navigation;           // Si requiere navla/navlo
} RgbStrategy;

// Funciones del ciclo de vida del contexto
void rgb_context_init(RgbContext *ctx);
void rgb_context_destroy(RgbContext *ctx);
bool rgb_parse_options(ArgParser *parser, RgbContext *ctx);

// Función principal
int run_rgb(ArgParser *parser);

// Funciones de composición RGB
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

ImageData create_truecolor_rgb(DataF c01_blue, DataF c02_red, DataF c03_nir);

ImageData create_truecolor_rgb_rayleigh(DataF c01_blue, DataF c02_red, DataF c03_nir,
                                        const char *filename_ref, bool apply_rayleigh);

#endif /* HPSATVIEWS_RGB_H_ */