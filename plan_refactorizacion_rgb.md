# Plan de Refactorización para `rgb.c`

## Objetivo Principal

El objetivo de esta refactorización es transformar `rgb.c` de un archivo con una función monolítica (`run_rgb`) a un módulo más cohesivo, encapsulado y fácil de mantener. Se busca mejorar la legibilidad, la robustez y la capacidad de prueba del código, aplicando los principios de responsabilidad única.

## Fases de la Refactorización

Proponemos dividir el trabajo en 3 fases principales, empezando por lo más crítico y avanzando hacia mejoras estructurales.

### Fase 1: Robustez y Gestión de Memoria (¡Completada!)

Esta fase era la más crítica para la estabilidad del programa.

*   **Acción:** Añadir verificaciones para todas las asignaciones de memoria (`malloc`, `strdup`, etc.) y gestionar los errores de forma controlada.
*   **Estado:** ✅ **Completado**. Ya hemos implementado las verificaciones en `channelset_create`, `normalize_to_u8` y `create_multiband_rgb`, previniendo "Segmentation Faults" y terminando el programa de forma segura en caso de fallo de memoria.

---

### Fase 2: Refactorización Estructural de `run_rgb`

Esta es la fase central. Consiste en descomponer la función `run_rgb` en funciones más pequeñas y especializadas.

#### 2.1. `parse_rgb_options()`
*   **Responsabilidad:** Extraer y validar todas las opciones de la línea de comandos que son relevantes para el módulo `rgb`.
*   **Beneficios:**
    *   Centraliza la lógica de parsing de argumentos.
    *   `run_rgb` ya no dependerá directamente de `ArgParser`.
    *   Facilita la adición de nuevas opciones en el futuro.
*   **Mejora: Manejo de Errores Explícito**
    *   Añadir campo `valid` y `error_msg` a la estructura para manejo robusto de errores.
    *   Validar modo contra lista de modos permitidos.
*   **Ejemplo de implementación mejorada:**
    ```c
    // Nueva estructura para pasar las opciones
    typedef struct {
        const char *mode;
        const char *input_file;
        bool apply_rayleigh;
        float gamma;
        bool apply_clahe;
        const char *clahe_params;
        // ... otras opciones
        
        // Campos para manejo de errores
        bool valid;              // true si parsing exitoso
        char error_msg[256];     // mensaje descriptivo de error
    } RgbOptions;

    /**
     * Parsea opciones CLI específicas del comando RGB.
     * 
     * @param parser  Parser de argumentos inicializado
     * @param opts    Estructura de salida (pre-allocada por caller)
     * @return        true si parsing exitoso, false si error crítico
     * 
     * @note En caso de error, opts->error_msg contendrá descripción
     * @note opts->valid será false si parsing falla
     */
    static bool parse_rgb_options(ArgParser *parser, RgbOptions *opts) {
        memset(opts, 0, sizeof(RgbOptions));
        opts->valid = false;
        
        // Validar archivo de entrada
        opts->input_file = ap_get_arg_at_index(parser, 0);
        if (!opts->input_file) {
            snprintf(opts->error_msg, sizeof(opts->error_msg), 
                     "Falta archivo NetCDF de entrada");
            return false;
        }
        
        // Obtener modo (con default)
        opts->mode = ap_get_str_value(parser, "mode");
        if (!opts->mode) opts->mode = "composite";
        
        // Validar modo contra lista permitida
        const char *valid_modes[] = {"composite", "truecolor", "night", 
                                      "ash", "airmass", "so2", NULL};
        if (!is_valid_mode(opts->mode, valid_modes)) {
            snprintf(opts->error_msg, sizeof(opts->error_msg),
                     "Modo inválido: %s", opts->mode);
            return false;
        }
        
        // Otras opciones
        opts->apply_rayleigh = ap_found(parser, "rayleigh");
        opts->gamma = ap_get_dbl_value(parser, "gamma");
        opts->apply_clahe = ap_found(parser, "clahe");
        opts->clahe_params = ap_get_str_value(parser, "clahe-params");
        // ...
        
        opts->valid = true;
        return true;
    }
    
    // Función auxiliar de validación
    static bool is_valid_mode(const char *mode, const char *valid_modes[]) {
        for (int i = 0; valid_modes[i] != NULL; i++) {
            if (strcmp(mode, valid_modes[i]) == 0) return true;
        }
        return false;
    }
    ```

#### 2.2. `load_and_prepare_channels()`
*   **Responsabilidad:** Gestionar la creación del `ChannelSet`, encontrar los archivos de los canales, cargarlos con `load_nc_sf` y aplicar el *downsampling* necesario.
*   **Beneficios:**
    *   Encapsula toda la lógica de búsqueda y carga de archivos.
    *   Simplifica enormemente el inicio de `run_rgb`.
*   **Mejora: Ownership y Error Handling**
    *   Documentar claramente ownership de memoria (caller responsable de liberar ChannelSet y DataNC).
    *   Retornar estructura de error explícita en caso de falla.
    *   Validar éxito de carga para cada canal requerido.
*   **Ejemplo de implementación mejorada:**
    ```c
    typedef struct {
        bool has_error;
        char message[256];
    } RgbError;
    
    /**
     * Carga y prepara canales NetCDF según configuración RGB.
     * 
     * @param opts     Opciones de configuración RGB
     * @param channels Puntero de salida para ChannelSet (allocado internamente)
     * @param c        Array de salida para datos NetCDF (pre-allocado por caller)
     * @param error    Estructura de salida para mensajes de error (opcional)
     * @return         true si carga exitosa, false si error
     * 
     * @note Caller responsable de liberar *channels con channelset_free()
     * @note Caller responsable de liberar c[] con datanc_free() para cada canal cargado
     * @note Si error != NULL, se llenará con información detallada del error
     */
    static bool load_and_prepare_channels(
        const RgbOptions *opts, 
        ChannelSet **channels, 
        DataNC c[17],
        RgbError *error
    ) {
        // 1. Crear el ChannelSet según opts->mode
        *channels = channelset_create(opts->mode);
        if (!*channels) {
            if (error) {
                error->has_error = true;
                snprintf(error->message, sizeof(error->message),
                         "Modo RGB inválido: %s", opts->mode);
            }
            return false;
        }
        
        // 2. Encontrar ID del satélite desde archivo de entrada
        char sat_id[32];
        if (!find_satellite_id(opts->input_file, sat_id, sizeof(sat_id))) {
            if (error) {
                error->has_error = true;
                snprintf(error->message, sizeof(error->message),
                         "No se pudo determinar ID del satélite");
            }
            channelset_free(*channels);
            return false;
        }
        
        // 3. Iterar y cargar cada canal requerido
        for (int i = 0; i < (*channels)->num_channels; i++) {
            const char *channel_name = (*channels)->channel_names[i];
            int idx = channel_to_index(channel_name);  // C01 -> 1, etc.
            
            if (!load_nc_sf(opts->input_directory, sat_id, channel_name, 
                           opts->scale_factor, &c[idx])) {
                if (error) {
                    error->has_error = true;
                    snprintf(error->message, sizeof(error->message),
                             "Fallo al cargar canal %s", channel_name);
                }
                // Cleanup parcial
                for (int j = 0; j < i; j++) {
                    int prev_idx = channel_to_index((*channels)->channel_names[j]);
                    datanc_free(&c[prev_idx]);
                }
                channelset_free(*channels);
                return false;
            }
        }
        
        // 4. Aplicar downsampling a canales de alta resolución (C01, C02, C03)
        const int high_res_channels[] = {1, 2, 3};
        for (int i = 0; i < 3; i++) {
            int idx = high_res_channels[i];
            if (c[idx].data != NULL) {  // Solo si fue cargado
                datanc_downsample(&c[idx], opts->downsample_factor);
            }
        }
        
        // 5. Validar que todos los canales requeridos están presentes
        for (int i = 0; i < (*channels)->num_channels; i++) {
            int idx = channel_to_index((*channels)->channel_names[i]);
            if (c[idx].data == NULL) {
                if (error) {
                    error->has_error = true;
                    snprintf(error->message, sizeof(error->message),
                             "Canal requerido %s no disponible", 
                             (*channels)->channel_names[i]);
                }
                // Cleanup completo
                for (int j = 0; j < 17; j++) {
                    if (c[j].data != NULL) datanc_free(&c[j]);
                }
                channelset_free(*channels);
                return false;
            }
        }
        
        if (error) error->has_error = false;
        return true;
    }
    ```

#### 2.3. `process_geospatial_data()`
*   **Responsabilidad:** Manejar toda la lógica geoespacial: cálculo de navegación, recorte (clip) y reproyección.
*   **Beneficios:**
    *   Aísla las complejas operaciones de reproyección y recorte.
    *   Permite modificar la lógica geoespacial sin afectar la composición de la imagen.
*   **Mejora: Separación de Navegación y Transformaciones**
    *   Dividir en `calculate_navigation()` y `apply_geospatial_transforms()`.
    *   Usar estructura GeospatialConfig para pasar parámetros.
    *   Documentar modificaciones in-place.
*   **Ejemplo de implementación mejorada:**
    ```c
    typedef struct {
        bool apply_clip;
        float clip_coords[4];  // minx, miny, maxx, maxy
        bool apply_reprojection;
        const char *target_projection;
        // ... otros parámetros geoespaciales
    } GeospatialConfig;
    
    /**
     * Calcula arrays de navegación (lat/lon) desde datos NetCDF.
     * 
     * @param c       Array de canales NetCDF
     * @param navla   Puntero de salida para latitudes (allocado internamente)
     * @param navlo   Puntero de salida para longitudes (allocado internamente)
     * @return        true si cálculo exitoso, false si error
     * 
     * @note Caller responsable de liberar navla y navlo con dataf_free()
     */
    static bool calculate_navigation(DataNC c[17], DataF *navla, DataF *navlo) {
        // Usar información de proyección desde primer canal válido
        int reference_channel = find_first_valid_channel(c, 17);
        if (reference_channel < 0) return false;
        
        // Calcular navegación desde metadatos del canal
        return compute_latlon_from_projection(&c[reference_channel], navla, navlo);
    }
    
    /**
     * Aplica transformaciones geoespaciales a canales cargados.
     * 
     * @param config  Configuración de transformaciones a aplicar
     * @param c       Array de canales NetCDF (modificado in-place)
     * @param navla   Array de latitudes (modificado in-place si hay reproj)
     * @param navlo   Array de longitudes (modificado in-place si hay reproj)
     * @param error   Estructura de salida para mensajes de error (opcional)
     * @return        true si éxito, false si error
     * 
     * @note c, navla, navlo son modificados in-place
     * @note Secuencia: 1) clip pre-reproj, 2) reproyección, 3) clip post-reproj
     */
    static bool apply_geospatial_transforms(
        const GeospatialConfig *config,
        DataNC c[17],
        DataF *navla,
        DataF *navlo,
        RgbError *error
    ) {
        // 1. Validar configuración
        if (!config) {
            if (error) {
                error->has_error = true;
                snprintf(error->message, sizeof(error->message),
                         "Configuración geoespacial inválida");
            }
            return false;
        }
        
        // 2. Aplicar recorte PRE-reproyección (si aplica)
        if (config->apply_clip && !config->apply_reprojection) {
            for (int i = 0; i < 17; i++) {
                if (c[i].data != NULL) {
                    datanc_clip(&c[i], config->clip_coords);
                }
            }
            dataf_clip(navla, config->clip_coords);
            dataf_clip(navlo, config->clip_coords);
        }
        
        // 3. Aplicar reproyección (si aplica)
        if (config->apply_reprojection) {
            if (!reprojection_apply(c, navla, navlo, config->target_projection)) {
                if (error) {
                    error->has_error = true;
                    snprintf(error->message, sizeof(error->message),
                             "Fallo en reproyección a %s", config->target_projection);
                }
                return false;
            }
        }
        
        // 4. Aplicar recorte POST-reproyección (si aplica)
        if (config->apply_clip && config->apply_reprojection) {
            for (int i = 0; i < 17; i++) {
                if (c[i].data != NULL) {
                    datanc_clip(&c[i], config->clip_coords);
                }
            }
            dataf_clip(navla, config->clip_coords);
            dataf_clip(navlo, config->clip_coords);
        }
        
        if (error) error->has_error = false;
        return true;
    }
    ```

#### 2.4. `compose_rgb_image()`
*   **Responsabilidad:** Contener el `switch` o `if/else` que decide qué producto RGB generar (`truecolor`, `ash`, `composite`, etc.) y llamar a la función de composición correspondiente.
*   **Beneficios:**
    *   Clarifica el punto de decisión para la creación de la imagen.
*   **Mejora: Tabla de Dispatch y Ownership Explícito**
    *   Usar tabla de punteros a función para eliminar if/else chain.
    *   Documentar ownership de memoria retornada.
    *   Validar modo antes de dispatch.
*   **Ejemplo de implementación mejorada:**
    ```c
    // Tipo de función para compositores RGB específicos
    typedef ImageData (*RgbComposer)(const RgbOptions *opts, DataNC c[17], 
                                      DataF *navla, DataF *navlo, RgbError *error);
    
    // Estructura de dispatch
    typedef struct {
        const char *mode_name;
        RgbComposer composer_func;
    } RgbComposerEntry;
    
    // Tabla de dispatch (debe estar ordenada alfabéticamente para bsearch)
    static const RgbComposerEntry RGB_COMPOSERS[] = {
        {"airmass",    compose_airmass},
        {"ash",        compose_ash},
        {"composite",  compose_composite_day_night},
        {"night",      compose_night},
        {"so2",        compose_so2},
        {"truecolor",  compose_truecolor},
        {NULL, NULL}  // Sentinel
    };
    
    /**
     * Compone imagen RGB final según modo especificado.
     * 
     * @param opts   Opciones de configuración RGB
     * @param c      Array de canales NetCDF cargados
     * @param navla  Array de latitudes
     * @param navlo  Array de longitudes
     * @param error  Estructura de salida para mensajes de error (opcional)
     * @return       ImageData con composición RGB
     * 
     * @note Caller responsable de liberar imagen con image_free()
     * @note Retorna imagen vacía (width=0) en caso de error
     */
    static ImageData compose_rgb_image(
        const RgbOptions *opts, 
        DataNC c[17], 
        DataF *navla, 
        DataF *navlo,
        RgbError *error
    ) {
        ImageData final_image = {0};
        
        // Buscar compositor correspondiente al modo
        RgbComposer composer = NULL;
        for (int i = 0; RGB_COMPOSERS[i].mode_name != NULL; i++) {
            if (strcmp(opts->mode, RGB_COMPOSERS[i].mode_name) == 0) {
                composer = RGB_COMPOSERS[i].composer_func;
                break;
            }
        }
        
        // Validar que el modo es reconocido
        if (!composer) {
            if (error) {
                error->has_error = true;
                snprintf(error->message, sizeof(error->message),
                         "Modo RGB no reconocido: %s", opts->mode);
            }
            return final_image;
        }
        
        // Dispatch a compositor específico
        final_image = composer(opts, c, navla, navlo, error);
        
        // Validar resultado
        if (final_image.width == 0 && error && !error->has_error) {
            error->has_error = true;
            snprintf(error->message, sizeof(error->message),
                     "Compositor %s retornó imagen vacía", opts->mode);
        }
        
        return final_image;  // Ownership transferido a caller
    }
    
    // Ejemplo de compositor específico
    static ImageData compose_truecolor(
        const RgbOptions *opts,
        DataNC c[17],
        DataF *navla,
        DataF *navlo,
        RgbError *error
    ) {
        ImageData img = create_truecolor_rgb_rayleigh(
            c[1], c[2], c[3],  // C01, C02, C03
            opts->apply_rayleigh,
            opts->gamma
        );
        
        if (img.width == 0 && error) {
            error->has_error = true;
            snprintf(error->message, sizeof(error->message),
                     "Fallo al crear truecolor RGB");
        }
        
        return img;
    }
    
    // Ejemplo de compositor composite (día/noche)
    static ImageData compose_composite_day_night(
        const RgbOptions *opts,
        DataNC c[17],
        DataF *navla,
        DataF *navlo,
        RgbError *error
    ) {
        // Crear imagen diurna
        ImageData diurna = compose_truecolor(opts, c, navla, navlo, error);
        if (diurna.width == 0) return diurna;
        
        // Crear imagen nocturna
        ImageData nocturna = compose_night(opts, c, navla, navlo, error);
        if (nocturna.width == 0) {
            image_free(diurna);
            return nocturna;
        }
        
        // Calcular máscara día/noche
        uint8_t *mask = daynight_compute_mask(navla, navlo);
        if (!mask) {
            if (error) {
                error->has_error = true;
                snprintf(error->message, sizeof(error->message),
                         "Fallo al calcular máscara día/noche");
            }
            image_free(diurna);
            image_free(nocturna);
            ImageData empty = {0};
            return empty;
        }
        
        // Blend con máscara
        ImageData result = blend_images_rgb_indexed(diurna, nocturna, mask);
        
        // Cleanup
        free(mask);
        image_free(diurna);
        image_free(nocturna);
        
        return result;  // Ownership transferido a caller
    }
    ```

---

### Fase 3: Modernización y Encapsulación

Esta fase se centra en eliminar "números mágicos" y modernizar módulos dependientes.

#### 3.1. Eliminar "Magic Numbers"
*   **Acción:** Reemplazar los rangos de valores hardcodeados para los falsos colores (`ash`, `airmass`, `so2`) por constantes con nombres descriptivos.
*   **Ejemplo:**
    *   **Antes:**
        ```c
        final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -6.7f, 2.6f, ...);
        ```
    *   **Después:**
        ```c
        #define ASH_RED_MIN -6.7f
        #define ASH_RED_MAX  2.6f
        // ...
        final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, ASH_RED_MIN, ASH_RED_MAX, ...);
        ```
*   **Beneficios:** El código se vuelve auto-documentado y es más fácil de ajustar.

#### 3.2. Modernizar `nocturnal_pseudocolor.c`
*   **Acción:** Refactorizar `create_nocturnal_pseudocolor` para que sea consistente con el resto del sistema.
    1.  Eliminar el `printf` y usar el sistema de `logging` (`LOG_INFO`, `LOG_DEBUG`).
    2.  Eliminar la dependencia del `paleta.h` hardcodeado.
    3.  Modificar la función para que acepte `const DataF*` y una paleta de colores (`const ColorArray*`) como parámetros.
    4.  **Cambiar la salida a una imagen indexada (`bpp=1`)** en lugar de una imagen RGB (`bpp=3`). Esto la hace más eficiente y consistente con `singlegray`.
*   **Beneficios:**
    *   Consistencia en todo el código.
    *   Mayor flexibilidad para generar imágenes nocturnas con diferentes paletas.
    *   Eficiencia de memoria para el modo `night`.
*   **Implicaciones:** Este cambio tiene un impacto directo en el modo `composite`, que requiere una solución específica (ver Fase 3.3).

#### 3.3. Optimizar el Mezclado en Modo `composite`
*   **Problema:** La modernización de `nocturnal_pseudocolor` (Fase 3.2) hace que la imagen nocturna sea indexada (`bpp=1`), mientras que la imagen diurna es RGB (`bpp=3`). La función `blend_images` actual solo sabe mezclar dos imágenes RGB.
*   **Solución Propuesta (Eficiente):** Crear una nueva función de mezclado especializada en `image.c`.
    *   **Nombre sugerido:** `blend_images_rgb_indexed()`
    *   **Firma mejorada:** `ImageData blend_images_rgb_indexed(ImageData rgb_image, ImageData indexed_image, uint8_t *mask)`
    *   **Lógica interna:**
        1.  **Pre-condiciones:** Validar que `rgb_image.bpp == 3`, `indexed_image.bpp == 1`, y que tienen mismas dimensiones.
        2.  **Allocar:** Crear nueva imagen RGB de salida con dimensiones de entrada.
        3.  **Iterar:** Para cada píxel `(x, y)`:
            *   Leer color RGB de fondo desde `rgb_image.data`.
            *   Leer índice desde `indexed_image.data`.
            *   **Expandir índice a RGB:** Usar paleta embebida desde `indexed_image.palette` (estructura ColorArray ya presente en ImageData).
            *   Calcular peso de mezcla desde `mask[y*width + x]` (0-255).
            *   Mezclar: `output = fg_rgb * alpha + bg_rgb * (1 - alpha)` donde `alpha = mask / 255.0`.
        4.  **Retornar:** Nueva imagen RGB con blend aplicado.
*   **Mejoras al diseño:**
    *   **Eliminar parámetro `palette`:** La estructura `ImageData` ya contiene `ColorArray *palette` para imágenes indexadas, no necesitamos pasarla por separado.
    *   **Simplificar máscara:** Cambiar de `ImageData mask` a `uint8_t *mask` (array plano más eficiente).
    *   **Documentar ownership:** Nueva imagen allocada internamente, caller debe liberar con `image_free()`.
    *   **Validación robusta:** Verificar dimensiones y tipos antes de procesar.
*   **Beneficios de esta solución:**
    *   **Alta Eficiencia:** Se evita crear una imagen RGB temporal de la parte nocturna, ahorrando una cantidad significativa de memoria (~84 MB para una imagen Full Disk) y el tiempo de CPU necesario para la conversión.
    *   **Encapsulación:** La lógica de cómo manejar una imagen indexada reside dentro de la función de mezclado, manteniendo el código de `rgb.c` más limpio.
    *   **Reutilización:** Aprovecha infraestructura existente de paletas en `ImageData`.
*   **Firma final sugerida:**
    ```c
    /**
     * Mezcla una imagen RGB con una imagen indexada usando máscara de opacidad.
     * 
     * @param rgb_image      Imagen de fondo RGB (bpp=3)
     * @param indexed_image  Imagen de frente indexada (bpp=1) con paleta embebida
     * @param mask           Máscara de opacidad (0-255, mismo tamaño que imágenes)
     * @return               Nueva imagen RGB con blend aplicado
     * 
     * @note Caller responsable de liberar imagen retornada con image_free()
     * @note Retorna imagen vacía (width=0) si dimensiones no coinciden o tipos inválidos
     * @note indexed_image.palette debe ser no-NULL
     * @note mask[i] = 0 → 100% rgb_image, mask[i] = 255 → 100% indexed_image
     */
    ImageData blend_images_rgb_indexed(ImageData rgb_image, ImageData indexed_image, const uint8_t *mask);
    ```

---

#### 3.4. Eliminar Rutas de Archivos "Hardcodeadas"
*   **Problema:** El código utiliza rutas fijas para archivos de datos, como `/usr/local/share/lanot/docs/recortes_coordenadas.csv` en `rgb.c` y `main.c`. Esto limita la portabilidad y dificulta la configuración.
*   **Solución Propuesta (Mejorada con Prioridad de Búsqueda):**
    1.  **Centralizar configuración:** Crear `config.h` con rutas por defecto y prefijos de búsqueda.
    2.  **Jerarquía de búsqueda:** Implementar función `resolve_config_path()` con orden de prioridad:
        *   **Prioridad 1:** Argumento CLI `--data-path <ruta>` (máxima precedencia).
        *   **Prioridad 2:** Variable de entorno `HPSATVIEWS_DATA_PATH`.
        *   **Prioridad 3:** Rutas relativas al ejecutable (`./ `, `../share/`, `../data/`).
        *   **Prioridad 4:** Ruta de instalación del sistema (`/usr/local/share/lanot`).
        *   **Prioridad 5:** Fallback a ruta compilada por defecto.
    3.  **Validación:** Verificar existencia de archivo antes de uso (evitar errores crípticos).
*   **Implementación sugerida:**
    ```c
    // config.h
    #ifndef CONFIG_H
    #define CONFIG_H
    
    // Rutas por defecto (pueden sobreescribirse con -D en Makefile)
    #ifndef HPSATVIEWS_DATA_DIR
    #define HPSATVIEWS_DATA_DIR "/usr/local/share/lanot"
    #endif
    
    #define DEFAULT_CLIP_FILE "docs/recortes_coordenadas.csv"
    #define DEFAULT_RAYLEIGH_LUT "data/rayleigh_lut.bin"
    
    /**
     * Resuelve ruta completa de archivo de configuración/datos.
     * 
     * @param relative_path  Ruta relativa al directorio de datos (ej. "docs/recortes.csv")
     * @param cli_override   Ruta especificada por CLI (puede ser NULL)
     * @param buffer         Buffer de salida para ruta completa
     * @param buffer_size    Tamaño del buffer
     * @return               true si archivo existe, false si no se encuentra
     * 
     * @note Búsqueda en orden: CLI → ENV → relativo a ejecutable → sistema → default
     * @note buffer contendrá ruta completa si retorna true
     */
    bool resolve_config_path(const char *relative_path, 
                            const char *cli_override,
                            char *buffer, 
                            size_t buffer_size);
    
    #endif // CONFIG_H
    ```
    
    ```c
    // config.c (nueva implementación)
    #include "config.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <unistd.h>
    #include <limits.h>
    
    bool resolve_config_path(const char *relative_path, 
                            const char *cli_override,
                            char *buffer, 
                            size_t buffer_size) {
        // Prioridad 1: Argumento CLI
        if (cli_override && access(cli_override, R_OK) == 0) {
            snprintf(buffer, buffer_size, "%s", cli_override);
            return true;
        }
        
        // Prioridad 2: Variable de entorno
        const char *env_path = getenv("HPSATVIEWS_DATA_PATH");
        if (env_path) {
            snprintf(buffer, buffer_size, "%s/%s", env_path, relative_path);
            if (access(buffer, R_OK) == 0) return true;
        }
        
        // Prioridad 3: Relativo al ejecutable
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            // Eliminar nombre del ejecutable
            char *last_slash = strrchr(exe_path, '/');
            if (last_slash) *last_slash = '\0';
            
            // Probar ./data/, ../share/, ../data/
            const char *relative_dirs[] = {"data", "../share/lanot", "../data", NULL};
            for (int i = 0; relative_dirs[i]; i++) {
                snprintf(buffer, buffer_size, "%s/%s/%s", 
                        exe_path, relative_dirs[i], relative_path);
                if (access(buffer, R_OK) == 0) return true;
            }
        }
        
        // Prioridad 4: Ruta de sistema
        snprintf(buffer, buffer_size, "%s/%s", HPSATVIEWS_DATA_DIR, relative_path);
        if (access(buffer, R_OK) == 0) return true;
        
        // No encontrado
        return false;
    }
    ```
*   **Beneficios de esta solución mejorada:**
    *   **Portabilidad máxima:** Funciona en instalaciones del sistema, desarrollo local, y entornos empaquetados.
    *   **Flexibilidad:** Usuarios pueden usar sus propios archivos sin recompilar.
    *   **Desarrollo amigable:** Busca automáticamente en directorios relativos al ejecutable (útil durante desarrollo).
    *   **Debugging mejorado:** Errores más claros cuando archivo no se encuentra.
    *   **Thread-safe:** No usa variables globales mutables (a diferencia de chdir).
*   **Consideración adicional:** Para entornos como AppImage o snap, agregar búsqueda en `$APPDIR/share` o `$SNAP/share`.

#### 3.5. Fondos de Luces Nocturnas (full disk vs CONUS)
*   **Objetivo:** Permitir un fondo opcional con luces de ciudad para el modo nocturno, seleccionando automáticamente el archivo según el dominio (full disk vs CONUS) sin rutas hardcodeadas.
*   **Archivo opcional:** `images/land_lights_2012_fd.png` (full disk) y `images/land_lights_2012_conus.png` (CONUS). Se consideran **assets opcionales**: el programa debe funcionar aunque no existan.
*   **Estrategia de resolución de ruta:** Reutilizar `resolve_config_path()` con `relative_path` dinámico:
    * Si `datanc.width == 2500` usar `"images/land_lights_2012_conus.png"`; en otro caso `"images/land_lights_2012_fd.png"`.
    * Orden de búsqueda: CLI → ENV → relativo al ejecutable → sistema → default, igual que en 3.4.
*   **Manejo de ausencia de archivos:**
    * Si el PNG no se encuentra o falla la carga, registrar `LOG_WARN` y **continuar sin fondo** (no abortar).
    * Devolver `ImageData` vacío (`width=0`) y hacer que el compositor nocturno ignore el blend cuando falte el fondo.
*   **Interfaz sugerida:**
    * Nuevo flag `--citylights` para habilitar el uso del fondo (por defecto desactivado para no depender de assets externos).
    * Opcional: `--citylights-path <png>` para forzar ruta personalizada.
*   **Integración en compositor nocturno:**
    * Cargar fondo si flag activo; si la carga falla, seguir con la imagen nocturna actual.
    * Para el modo `composite`, la función `compose_composite_day_night` mezcla `diurna` con `nocturna`; el fondo de luces se aplica **antes** de mezclar o como capa adicional detrás de `nocturna` usando `blend_images_rgb_indexed` (si `nocturna` es indexada) o `blend_images` (si es RGB).
*   **Beneficios:**
    * Evita rutas fijas y fallas al instalar sin assets.
    * Permite distribución ligera (sin PNG) manteniendo funcionalidad básica.
    * Usuarios avanzados pueden agregar sus propios mapas de luces sin recompilar.

---

## Resumen de Mejoras Expertas Integradas

Este plan ha sido revisado y mejorado por un experto en desarrollo C11, incorporando las siguientes mejoras críticas:

### Manejo de Errores Robusto (Fase 2.1)
- **Estructura `RgbError`:** Manejo explícito de errores con mensajes descriptivos
- **Campo `valid` en `RgbOptions`:** Validación de estado para detectar errores de parsing
- **Validación de modos:** Lista blanca de modos permitidos para evitar valores inválidos

### Ownership de Memoria Explícito (Fases 2.2, 2.3, 2.4)
- **Documentación clara:** Cada función especifica quién es responsable de liberar memoria
- **Patrón consistente:** Caller siempre responsable de llamar `image_free()`, `datanc_free()`, etc.
- **Cleanup en caso de error:** Liberación explícita de recursos parcialmente allocados

### Arquitectura de Dispatch (Fase 2.4)
- **Tabla de compositores:** Elimina if/else chain con tabla de punteros a función
- **Extensibilidad:** Nuevos modos RGB se agregan solo modificando tabla, no lógica
- **Type-safety:** Tipo `RgbComposer` para garantizar firmas consistentes

### Separación de Responsabilidades (Fase 2.3)
- **`calculate_navigation()`:** Calcula lat/lon desde metadatos NetCDF
- **`apply_geospatial_transforms()`:** Aplica clip y reproyección con orden documentado
- **`GeospatialConfig`:** Estructura dedicada para parámetros geoespaciales

### Optimización de Blend (Fase 3.3)
- **Firma simplificada:** Elimina parámetro redundante `palette` (ya en `ImageData`)
- **Máscara eficiente:** `uint8_t *mask` en lugar de `ImageData mask`
- **Reutilización:** Aprovecha infraestructura existente de `ColorArray` en `ImageData`

### Sistema de Configuración Portable (Fase 3.4)
- **Jerarquía de búsqueda:** CLI → ENV → relativo → sistema → default
- **Thread-safe:** No usa variables globales mutables
- **Desarrollo amigable:** Busca automáticamente en directorios relativos al ejecutable
- **Validación:** Verifica existencia de archivo antes de uso

### Recomendaciones de Prioridad
1. **Prioridad Alta:** Fase 2.1 + 2.2 (parsing y carga) - impacto inmediato en robustez
2. **Prioridad Media:** Fase 2.3 + 2.4 (geoespacial y composición) - mejora arquitectura
3. **Prioridad Baja:** Fase 3.1-3.2 (magic numbers y nocturnal) - mejoras de calidad
4. **Prioridad Opcional:** Fase 3.4 (config paths) - beneficio para distribución

### Estrategia de Testing Recomendada
- **Tests unitarios:** Para `parse_rgb_options()`, `resolve_config_path()`
- **Tests de integración:** Para cada compositor RGB (`compose_truecolor`, etc.)
- **Casos de error:** Validar cleanup correcto en caso de fallos parciales
- **Memoria:** Usar Valgrind para detectar leaks durante refactorización

### Notas de Implementación
- **Commits incrementales:** Un commit por subfase (2.1, 2.2, 2.3, etc.)
- **Compatibilidad:** Mantener interfaz pública de `run_rgb()` durante transición
- **Documentación:** Actualizar comentarios de función con formato Doxygen
- **Validación:** Compilar con `-Wall -Wextra -Werror` para detectar warnings

---

**Sugerencia Final:** Empezar por la **Fase 2 (2.1 → 2.2 → 2.3 → 2.4)**, ya que la refactorización estructural de `run_rgb` es la que tendrá el mayor impacto en la manejabilidad y robustez del código. La Fase 1 ya está completa, y la Fase 3 puede implementarse de forma incremental después.