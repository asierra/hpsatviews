/*
 * RGB and day/night composite generation module.
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_RGB_H_
#define HPSATVIEWS_RGB_H_

#include <stdbool.h>
#include "channelset.h"
#include "datanc.h"
#include "image.h"
#include "config.h"
#include "metadata.h"

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
    bool rayleigh_analytic;        // --ray-analytic (usa fórmula analítica en lugar de LUTs)
    bool use_piecewise_stretch;    // --stretch (realce de contraste por tramos)
    bool use_citylights;           // --citylights (solo night/composite)
    bool use_alpha;                // --alpha
    bool force_geotiff;            // --geotiff
    bool use_full_res;             // --full-res
    
    // custom
    char *expr;
    char *minmax;
    
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
    
    // --- Canales intermedios ---
    DataF comp_r;
    DataF comp_g;
    DataF comp_b;
    
    // --- Límites de visualización (Scaling) ---
    float min_r, max_r;
    float min_g, max_g;
    float min_b, max_b;
    
    // Resultado final
    ImageData final_image;
    ImageData alpha_mask;

    // Estado de error
    bool error_occurred;
    char error_msg[512];
} RgbContext;

// Declaración del tipo de función para compositores RGB
typedef bool (*RgbComposer)(RgbContext *ctx);

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

/**
 * @brief Inicializa el contexto RGB a sus valores por defecto.
 * Limpia la memoria y establece los valores predeterminados para las opciones.
 * @param ctx Puntero al contexto a inicializar.
 */
void rgb_context_init(RgbContext *ctx);

/**
 * @brief Libera toda la memoria dinámica contenida en el contexto RGB.
 * Es seguro llamar a esta función incluso si algunos punteros son NULL.
 * @param ctx Puntero al contexto a destruir.
 */
void rgb_context_destroy(RgbContext *ctx);

/**
 * @brief Parsea los argumentos de la línea de comandos y puebla la estructura RgbOptions.
 * Centraliza toda la lógica de parsing de argumentos para el comando 'rgb'.
 * @param parser El parser de argumentos inicializado.
 * @param ctx El contexto RGB donde se guardarán las opciones y los mensajes de error.
 * @return true si el parsing fue exitoso, false en caso de error.
 */
bool rgb_parse_options(ArgParser *parser, RgbContext *ctx);

/**
 * @brief Procesamiento RGB con inyección de dependencias.
 * 
 * @param cfg Configuración inmutable del proceso (entrada).
 * @param meta Contexto de metadatos para acumular información (salida).
 * @return 0 en éxito, != 0 en error.
 */
int run_rgb(const ProcessConfig *cfg, MetadataContext *meta);

// Funciones de composición RGB
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

#endif /* HPSATVIEWS_RGB_H_ */
