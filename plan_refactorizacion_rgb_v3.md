# Plan de Refactorización RGB v3.1: Arquitectura "Context & Strategy" (Refinado)

**Fecha:** Diciembre 2025
**Objetivo:** Transformar `rgb.c` en un módulo modular, seguro y extensible, eliminando la complejidad ciclomática y centralizando la gestión de recursos.

**Cambios v3.1:** - Eliminación de buffers temporales globales (riesgo de *race conditions* o *leaks*).
- Validación de límites en arrays de canales.
- Clarificación de ownership de `ImageData` en compositores anidados.

---

## 🛠 Fase 1: Infraestructura de Estado (The Context)

El objetivo es centralizar todo el estado y opciones en una estructura, eliminando la "explosión de argumentos" en las funciones.

- [ ] **1.1. Definir `RgbOptions`**
    Crear estructura en `rgb.h` que contenga todas las opciones de configuración (Input):
    ```c
    typedef struct {
        // Identificadores y Rutas
        char *input_path;       // Directorio raíz de datos
        char *output_filename;  // Nombre archivo salida (CLI)
        char *prefix_nc;        // Prefijo archivos NC
        
        // Parámetros de Procesamiento
        char *mode;             // "truecolor", "ash", etc.
        float gamma;
        int scale;              // Factor de escala (negativo=down, positivo=up)
        bool verbose;
        
        // Parámetros Geográficos (Opcional)
        float crop_lon_w, crop_lon_e, crop_lat_n, crop_lat_s;
        bool crop_enabled;
    } RgbOptions;
    ```

- [ ] **1.2. Definir `RgbContext` (State Container)**
    Esta estructura mantiene el estado vivo durante la ejecución. **Nota:** No incluir variables temporales de cálculo aquí para asegurar aislamiento.
    ```c
    typedef struct {
        // Configuración (Input - Read Only)
        RgbOptions opts; 
        
        // Estado de Datos (Resources)
        DataNC *channels;       // Array dinámico o estático [17]
        DataF nav_lat;          // Matriz Latitud
        DataF nav_lon;          // Matriz Longitud
        bool has_navigation;    // Flag si nav fue cargada

        // Resultados (Output)
        ImageData final_image;
        bool output_generated;  // Flag para saber si hay que guardar
        
        // Diagnóstico
        char error_msg[512];
        bool has_error;
        
        // Metadatos internos
        int ref_channel_idx;    // Índice del canal de referencia para proyección
    } RgbContext;
    ```

- [ ] **1.3. Implementar Ciclo de Vida**
    * `void rgb_context_init(RgbContext *ctx);` (memset 0, defaults).
    * `void rgb_context_destroy(RgbContext *ctx);` (Libera channels, nav, final_image si no son NULL).

- [ ] **1.4. Refactorizar Parsing**
    * `bool parse_rgb_options(ArgParser *parser, RgbContext *ctx);`
    * Debe poblar `ctx->opts`.

---

## 🧠 Fase 2: Patrón Estrategia (The Composers)

Aislar la lógica de cada producto. Cada función debe ser autónoma.

- [ ] **2.1. Definir Contrato de Estrategia**
    ```c
    // Puntero a función que recibe el contexto y devuelve una imagen
    typedef ImageData (*RgbComposer)(RgbContext *ctx);

    typedef struct {
        const char *mode_name;           // ej. "ash"
        const char *req_channels[8];     // ej. {"C11", "C13", "C14", "C15", NULL}
        bool needs_navigation;           // Optimization Hint
        RgbComposer composer_func;       // La función que hace el trabajo
    } RgbStrategy;
    ```

- [ ] **2.2. Implementar Compositores (Wrappers)**
    Cada función debe declarar sus propias variables `DataF` temporales y liberarlas antes de retornar.

    * **2.2.1 `compose_truecolor`:** Usa C01, C02, C03. Llama a `create_truecolor_rgb_rayleigh`.
    * **2.2.2 `compose_ash`:** Usa C11, C13, C14, C15. Lógica multibanda.
    * **2.2.3 `compose_airmass`:** Usa C08, C10, C12, C13.
    * **2.2.4 `compose_night`:** Usa C13. Lógica simple pseudocolor.
    * **2.2.5 `compose_cloudtop`:** Usa C13.
    
    * **2.2.6 `compose_composite` (Caso Especial):**
        Este modo orquesta otros modos.
        ```c
        static ImageData compose_composite(RgbContext *ctx) {
            // 1. Generar capa diurna
            ImageData day = compose_truecolor(ctx);
            // 2. Generar capa nocturna
            ImageData night = compose_night(ctx);
            // 3. Generar máscara
            ImageData mask = create_daynight_mask(...);
            
            // 4. Blend
            ImageData result = blend_images(night, day, mask);
            
            // 5. CRÍTICO: Liberar intermedias
            image_destroy(&day);
            image_destroy(&night);
            image_destroy(&mask);
            
            return result;
        }
        ```

- [ ] **2.3. Tabla de Despacho**
    Crear array estático `STRATEGIES[]` mapeando nombres a funciones y canales requeridos.

---

## ⚙️ Fase 3: Pipeline Principal (The Runner)

Reescribir el flujo de control para que sea lineal.

- [ ] **3.1. `load_channels_for_strategy`**
    Función inteligente que solo carga lo necesario.
    * **Seguridad:** Usar límite `MAX_REQ_CHANNELS` en el bucle.
    ```c
    // Ejemplo lógica segura
    for(int i=0; i < MAX_REQ_CHANNELS && strategy->req_channels[i] != NULL; i++) {
        // Cargar canal...
    }
    ```

- [ ] **3.2. `process_geospatial`**
    Si `strategy->needs_navigation` es true:
    1. Cargar/Calcular Nav (Lat/Lon).
    2. Calcular recortes (Crop).
    3. Reproyectar canales cargados en `ctx->channels` (In-place modification).

- [ ] **3.3. `resolve_output_filename`**
    Lógica para determinar nombre de salida si no se provee por CLI.

- [ ] **3.4. `resolve_config_path`**
    Sistema de prioridades para encontrar archivos de configuración (Mapas, Palette, etc.):
    1. Argumento CLI (si existiera)
    2. Variable Entorno `HPSAT_CONFIG_DIR`
    3. Ruta relativa `./config/`
    4. Ruta sistema `/usr/local/share/hpsatviews/`

- [ ] **3.5. `run_rgb` (Nueva Implementación)**
    Flujo limpio con manejo de errores unificado.
    ```c
    int run_rgb(ArgParser *parser) {
        RgbContext ctx;
        rgb_context_init(&ctx);
        int ret = 0;

        if (!parse_rgb_options(parser, &ctx)) goto error;
        
        const RgbStrategy *strat = get_strategy(ctx.opts.mode);
        if (!strat) goto error;

        if (!load_channels_for_strategy(&ctx, strat)) goto error;
        
        if (!process_geospatial(&ctx, strat)) goto error;

        // Ejecución Polimórfica
        ctx.final_image = strat->composer_func(&ctx);
        if (!ctx.final_image.data) goto error;

        apply_post_processing(&ctx); // Gamma, etc.
        save_output(&ctx);

        goto cleanup; // Éxito

    error:
        ret = -1;
        LOG_ERROR("%s", ctx.error_msg);

    cleanup:
        rgb_context_destroy(&ctx); // Libera todo automáticamente
        return ret;
    }
    ```

---

## 🚀 Fase 4: Optimización y Testing

- [ ] **4.1. Test Unitario de Estrategias:** Probar `compose_ash` aisladamente inyectando datos falsos en el Contexto.
- [ ] **4.2. Validación Valgrind:** Verificar que `rgb_context_destroy` limpia absolutamente todo.
- [ ] **4.3. Optimización Blending (Opcional):** Implementar mezcla indexada para ahorrar RAM en modo composite.

---

## 📝 Reglas de Oro para la Implementación

1. **Variables Locales vs Contexto:** Usa el `ctx` solo para datos que deben sobrevivir entre fases (Canales cargados, Navegación, Resultado final). Variables intermedias (`DataF` de restas, multiplicaciones) deben ser locales a la función y liberadas ahí mismo.
2. **Defensive Coding:** Siempre verificar punteros `NULL` antes de usar `image_destroy` o `dataf_destroy` (aunque estas funciones suelen manejar NULL, es buena práctica en la lógica de control).
3. **Logging:** Usar `snprintf(ctx->error_msg, ...)` antes de hacer `goto error` para saber exactamente qué falló.
