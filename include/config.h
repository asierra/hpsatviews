#ifndef HPSATVIEWS_CONFIG_H_
#define HPSATVIEWS_CONFIG_H_

#include <stdbool.h>

/**
 * Configuración inmutable del proceso.
 * Contiene todas las opciones solicitadas por el usuario (Input).
 * Reemplaza el paso de múltiples argumentos y estructuras ad-hoc como RgbOptions.
 */
typedef struct {
    // --- Archivo de Entrada ---
    const char *input_file;     // Path al archivo NetCDF de entrada
    bool is_l2_product;         // true si es CMIP (detectado del nombre)
    
    // --- Modo de Operación ---
    const char *command;        // "rgb", "gray", "pseudocolor"
    const char *strategy;       // "truecolor", "ch13", "ash", etc.
    
    // --- Parámetros Físicos y Realce ---
    float gamma;
    bool apply_clahe;
    float clahe_clip_limit;
    int clahe_tiles_x;
    int clahe_tiles_y;
    bool apply_histogram;
    bool apply_rayleigh;        // Corrección atmosférica (LUTs)
    bool rayleigh_analytic;     // Corrección Rayleigh analítica
    bool invert_values;         // Invertir escala (para canales IR)
    
    // --- Opciones de Composición ---
    int scale;                  // Factor de escala (upscaling/downsampling)
    bool use_alpha;             // Generar canal alfa
    bool use_citylights;        // Fondo de luces nocturnas
    bool use_full_res;          // Usar resolución completa (productos L2)

    // --- Álgebra de Bandas (Custom Mode) ---
    bool is_custom_mode;
    const char *custom_expr;    // Expresión del usuario (ej: "0.5*C02+0.3*C03")
    const char *custom_minmax;  // Rango min,max opcional
    
    // --- Pseudocolor (Paletas) ---
    const char *palette_file;   // Path al archivo .cpt (NULL si no aplica)
    
    // --- Geometría Solicitada ---
    bool has_clip;
    float clip_coords[4];       // [lon_min, lat_max, lon_max, lat_min]
    bool do_reprojection;
    
    // --- Salida ---
    bool force_geotiff;
    const char *output_path_override; // NULL para automático

} ProcessConfig;

// Forward declaration para evitar incluir args.h aquí
typedef struct ArgParser ArgParser;

/**
 * Convierte un ArgParser en un ProcessConfig inmutable.
 * Centraliza toda la lógica de parseo de argumentos.
 * 
 * @param parser ArgParser ya inicializado y parseado.
 * @param cfg ProcessConfig a llenar (debe estar inicializado a 0).
 * @return true si el parseo fue exitoso, false en caso de error.
 */
bool config_from_argparser(ArgParser *parser, ProcessConfig *cfg);

/**
 * Valida la configuración generada.
 * @param cfg Puntero a la configuración a validar.
 * @return true si es válida, false si hay errores lógicos.
 */
bool config_validate(const ProcessConfig *cfg);

/**
 * Imprime la configuración actual para debug.
 */
void config_print_debug(const ProcessConfig *cfg);

/**
 * Libera memoria dinámica asociada al ProcessConfig.
 * Solo libera campos alojados dinámicamente (output_path_override).
 */
void config_destroy(ProcessConfig *cfg);

#endif // HPSATVIEWS_CONFIG_H_
