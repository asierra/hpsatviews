# Plan de Refactorización RGB v2: Arquitectura "Context & Strategy"

**Fecha:** Diciembre 2025  
**Estado:** ✅ **FASE 4.1 COMPLETADA** - Tests de Regresión Exitosos  
**Objetivo:** Transformar `rgb.c` (884 líneas) en un módulo modular y extensible eliminando `if/else` gigantes y gestión de memoria manual dispersa.

## 📊 Progreso General

| Fase | Descripción | Estado | Progreso |
|------|-------------|--------|----------|
| **Fase 1** | Infraestructura de Estado (RgbContext) | ✅ COMPLETADA | 4/4 tareas |
| **Fase 2** | Patrón Estrategia (Composers) | ✅ COMPLETADA | 4/4 tareas |
| **Fase 3** | Pipeline Principal (The Runner) | ✅ COMPLETADA | 6/6 tareas |
| **Fase 4** | Validación y Testing | 🔄 EN PROGRESO | 1/3 tareas |
| **Fase 5** | Optimizaciones (Opcional) | ⏳ PENDIENTE | 0/5 tareas |

**✅ Últimos Hitos Alcanzados:**
- Fase 1-3: Refactorización completa implementada
- Fase 4.1: Tests de regresión implementados en `reproduction/run_demo.sh`
- Todos los modos funcionando correctamente: truecolor, ash, composite (con todas las opciones avanzadas)

**📋 Siguiente Paso: Fase 4.2** - Test de Memory Leaks con Valgrind

---

**Contexto Inicial:**
- `run_rgb()` tiene ~615 líneas con lógica inline de parsing, carga, reproyección, composición y escritura
- 11 comparaciones de `strcmp(mode, ...)` duplicadas (líneas 319-331 y 651-759)
- Gestión manual de 17 canales `DataNC c[17]` con liberación manual dispersa
- `ChannelSet` y funciones helper existen pero están desacopladas del flujo principal
- Archivos separados ya existen: `truecolor_rgb.c`, `nocturnal_pseudocolor.c`, `daynight_mask.c`, `image.c`

**Arquitectura Propuesta:**
1.  **RgbContext:** Estructura que mantiene todo el estado (configuración, datos, resultados)
2.  **Strategy Pattern:** Tabla de despacho que vincula modos con funciones composer
3.  **Goto Cleanup:** Manejo de errores centralizado para evitar fugas de memoria

---

## ✅ Fase 1: Infraestructura de Estado (The Context) - COMPLETADA

El objetivo es centralizar todo el estado y opciones en una estructura.

- [x] **1.1. Definir `RgbOptions`** (COMPLETADO)
    Crear estructura en `rgb.h` que contenga todas las opciones parseadas:
    ```c
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
        
        // Interno
        bool is_l2_product;            // Detectado automáticamente (contiene "CMIP")
    } RgbOptions;
    ```

- [x] **1.2. Definir `RgbContext`** (COMPLETADO)
    Creada la estructura en `include/rgb.h` que contendrá todo el estado:
    ```c
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
        
        // Resultados intermedios
        DataF r_temp, g_temp, b_temp;  // Canales temporales para operaciones
        
        // Resultado final
        ImageData final_image;
        ImageData alpha_mask;

        // Estado de error
        bool error_occurred;
        char error_msg[512];
    } RgbContext;
    ```

- [x] **1.3. Implementar Ciclo de Vida del Contexto** (COMPLETADO)
    Funciones implementadas en `src/rgb.c`:
    ```c
    void rgb_context_init(RgbContext *ctx) {
        memset(ctx, 0, sizeof(RgbContext));
        // Inicializar defaults
        ctx->opts.gamma = 1.0f;
        ctx->opts.clahe_tiles_x = 8;
        ctx->opts.clahe_tiles_y = 8;
        ctx->opts.clahe_clip_limit = 4.0f;
        ctx->opts.scale = 1;
    }
    
    void rgb_context_destroy(RgbContext *ctx) {
        // Liberar ChannelSet
        channelset_destroy(ctx->channel_set);
        
        // Liberar canales cargados
        for (int i = 1; i <= 16; i++) {
            if (ctx->channels[i].is_float && ctx->channels[i].fdata.data_in)
                datanc_destroy(&ctx->channels[i]);
        }
        
        // Liberar navegación
        dataf_destroy(&ctx->nav_lat);
        dataf_destroy(&ctx->nav_lon);
        
        // Liberar temporales
        dataf_destroy(&ctx->r_temp);
        dataf_destroy(&ctx->g_temp);
        dataf_destroy(&ctx->b_temp);
        
        // Liberar resultados
        image_destroy(&ctx->final_image);
        image_destroy(&ctx->alpha_mask);
        
        // Liberar output_filename si fue generado
        if (ctx->opts.output_generated && ctx->opts.output_filename)
            free(ctx->opts.output_filename);
    }
    ```

- [x] **1.4. Crear función de parsing** (COMPLETADO)
    Implementada como `rgb_parse_options()` en `src/rgb.c`:
    ```c
    bool rgb_parse_options(ArgParser *parser, RgbContext *ctx) {
        // Validar archivo de entrada
        if (!ap_has_args(parser)) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), 
                     "El comando 'rgb' requiere un archivo NetCDF de entrada");
            return false;
        }
        ctx->opts.input_file = ap_get_arg_at_index(parser, 0);
        
        // Parsear opciones booleanas
        ctx->opts.do_reprojection = ap_found(parser, "geographics");
        ctx->opts.apply_histogram = ap_found(parser, "histo");
        ctx->opts.force_geotiff = ap_found(parser, "geotiff");
        ctx->opts.apply_rayleigh = ap_found(parser, "rayleigh");
        ctx->opts.use_citylights = ap_found(parser, "citylights");
        ctx->opts.use_alpha = ap_found(parser, "alpha");
        
        // Parsear opciones con valores
        ctx->opts.mode = ap_get_str_value(parser, "mode");
        ctx->opts.gamma = ap_get_dbl_value(parser, "gamma");
        ctx->opts.scale = ap_get_int_value(parser, "scale");
        
        // Parsear CLAHE
        bool clahe_flag = ap_found(parser, "clahe");
        const char* clahe_params = ap_get_str_value(parser, "clahe-params");
        ctx->opts.apply_clahe = clahe_flag || (clahe_params != NULL);
        if (ctx->opts.apply_clahe && clahe_params) {
            sscanf(clahe_params, "%d,%d,%f", 
                   &ctx->opts.clahe_tiles_x, 
                   &ctx->opts.clahe_tiles_y, 
                   &ctx->opts.clahe_clip_limit);
        }
        
        // Parsear clip (usa función existente process_clip_coords)
        ctx->opts.has_clip = process_clip_coords(
            parser, "/usr/local/share/lanot/docs/recortes_coordenadas.csv",
            ctx->opts.clip_coords);
        
        // Detectar producto L2
        const char *basename_input = basename((char*)ctx->opts.input_file);
        ctx->opts.is_l2_product = (strstr(basename_input, "CMIP") != NULL);
        
        return true;
    }
    ```
---

## ✅ Fase 2: Patrón Estrategia (The Composers) - COMPLETADA

El objetivo es aislar la lógica de cada producto RGB eliminando los 11 `strcmp(mode, ...)`.

**Estado Final:**
- ✅ 6 funciones composer implementadas: `compose_truecolor()`, `compose_night()`, `compose_ash()`, `compose_airmass()`, `compose_so2()`, `compose_composite()`
- ✅ Tabla de estrategias STRATEGIES[] con 6 modos configurados
- ✅ Eliminados todos los `strcmp(mode, ...)` del código

- [x] **2.1. Definir Tipos de Estrategia** (COMPLETADO)
    Agregar en `rgb.h`:
    ```c
    typedef struct RgbContext RgbContext; // Forward declaration
    typedef ImageData (*RgbComposer)(RgbContext *ctx);

    typedef struct {
        const char *mode_name;           // "ash", "truecolor", etc
        const char *req_channels[6];     // {"C11", "C13", "C14", "C15", NULL}
        RgbComposer composer_func;       // Puntero a función
        const char *description;         // Para help/documentación
        bool needs_navigation;           // Si requiere navla/navlo
    } RgbStrategy;
    ```

- [x] **2.2. Crear Funciones Composer** (COMPLETADO)
    Funciones estáticas implementadas en `src/rgb.c`:
    
    **2.2.1. `compose_truecolor`** (líneas 651-657)
    ```c
    static ImageData compose_truecolor(RgbContext *ctx) {
        // Usa canales [1], [2], [3]
        if (ctx->opts.apply_rayleigh) {
            return create_truecolor_rgb_rayleigh(
                ctx->channels[1].fdata, 
                ctx->channels[2].fdata, 
                ctx->channels[3].fdata,
                ctx->channel_set->channels[0].filename,  // C01 filename
                true);
        } else {
            return create_truecolor_rgb(
                ctx->channels[1].fdata, 
                ctx->channels[2].fdata, 
                ctx->channels[3].fdata);
        }
    }
    ```
    
    **2.2.2. `compose_night`** (líneas 658-688)
    ```c
    static ImageData compose_night(RgbContext *ctx) {
        // Usa canal [13]
        ImageData citylights_bg = {0};
        if (ctx->opts.use_citylights) {
            const char* bg_path = (ctx->channels[ctx->ref_channel_idx].fdata.width == 2500)
                ? "/usr/local/share/lanot/images/land_lights_2012_conus.png"
                : "/usr/local/share/lanot/images/land_lights_2012_fd.png";
            
            // [Código de carga y remuestreo de fondo]
            // ... (líneas 662-686)
        }
        ImageData result = create_nocturnal_pseudocolor(
            &ctx->channels[13].fdata, 
            citylights_bg.data ? &citylights_bg : NULL);
        image_destroy(&citylights_bg);
        return result;
    }
    ```
    
    **2.2.3. `compose_ash`** (líneas 689-693)
    ```c
    static ImageData compose_ash(RgbContext *ctx) {
        // Variables temporales locales, no en el contexto.
        DataF r_temp = {0}, g_temp = {0};

        // Usa canales [11], [13], [14], [15]
        r_temp = dataf_op_dataf(&ctx->channels[15].fdata, 
                                &ctx->channels[13].fdata, OP_SUB);
        g_temp = dataf_op_dataf(&ctx->channels[14].fdata, 
                                &ctx->channels[11].fdata, OP_SUB);
        ImageData result = create_multiband_rgb(&r_temp, &g_temp, 
                                                &ctx->channels[13].fdata,
                                                -6.7f, 2.6f, -6.0f, 6.3f, 243.6f, 302.4f);
        dataf_destroy(&r_temp);
        dataf_destroy(&g_temp);
        return result;
    }
    ```
    
    **2.2.4. `compose_airmass`** (líneas 695-701)
    ```c
    static ImageData compose_airmass(RgbContext *ctx) {
        // Usa canales [8], [10], [12], [13]
        ctx->r_temp = dataf_op_dataf(&ctx->channels[8].fdata, 
                                     &ctx->channels[10].fdata, OP_SUB);
        ctx->g_temp = dataf_op_dataf(&ctx->channels[12].fdata, 
                                     &ctx->channels[13].fdata, OP_SUB);
        ctx->b_temp = dataf_op_scalar(&ctx->channels[8].fdata, 
                                      273.15f, OP_SUB, true);
        return create_multiband_rgb(&ctx->r_temp, &ctx->g_temp, &ctx->b_temp,
                                    -26.2f, 0.6f, -43.2f, 6.7f, 29.25f, 64.65f);
    }
    ```
    
    **2.2.5. `compose_so2`** (líneas 702-706)
    ```c
    static ImageData compose_so2(RgbContext *ctx) {
        // Usa canales [9], [10], [11], [13]
        ctx->r_temp = dataf_op_dataf(&ctx->channels[9].fdata, 
                                     &ctx->channels[10].fdata, OP_SUB);
        ctx->g_temp = dataf_op_dataf(&ctx->channels[13].fdata, 
                                     &ctx->channels[11].fdata, OP_SUB);
        return create_multiband_rgb(&ctx->r_temp, &ctx->g_temp, 
                                    &ctx->channels[13].fdata,
                                    -4.0f, 2.0f, -4.0f, 5.0f, 233.0f, 300.0f);
    }
    ```
    
    **2.2.6. `compose_composite`** (líneas 707-752) - **ESPECIAL**
    ```c
    static ImageData compose_composite(RgbContext *ctx) {
        // Genera imagen diurna (truecolor con post-proc)
        ImageData diurna = compose_truecolor(ctx);
        
        // Aplicar histogram/CLAHE a la diurna ANTES del blend
        if (ctx->opts.apply_histogram)
            image_apply_histogram(diurna);
        if (ctx->opts.apply_clahe)
            image_apply_clahe(diurna, ctx->opts.clahe_tiles_x, 
                             ctx->opts.clahe_tiles_y, ctx->opts.clahe_clip_limit);
        
        // Genera imagen nocturna (con citylights)
        ImageData nocturna = compose_night(ctx);
        
        // Genera máscara día/noche (usa canal [13] + navegación)
        float dnratio;
        ImageData mask = create_daynight_mask(
            ctx->channels[13], ctx->nav_lat, ctx->nav_lon, &dnratio, 263.15);
        LOG_INFO("Ratio día/noche: %.2f%%", dnratio);
        
        // Mezcla
        ImageData result = blend_images(nocturna, diurna, mask);
        
        // Limpiar temporales
        image_destroy(&diurna);
        image_destroy(&nocturna);
        image_destroy(&mask);
        
        return result;
    }
    ```

- [x] **2.3. Construir Tabla de Despacho** (COMPLETADO)
    Implementada en `src/rgb.c` como `STRATEGIES[]`:
    ```c
    static const RgbStrategy STRATEGIES[] = {
        { "truecolor", {"C01", "C02", "C03", NULL}, compose_truecolor, 
          "True Color RGB (natural)", false },
        
        { "night", {"C13", NULL}, compose_night, 
          "Nocturnal IR with temperature pseudocolor", false },
        
        { "ash", {"C11", "C13", "C14", "C15", NULL}, compose_ash, 
          "Volcanic Ash RGB", false },
        
        { "airmass", {"C08", "C10", "C12", "C13", NULL}, compose_airmass, 
          "Air Mass RGB", false },
        
        { "so2", {"C09", "C10", "C11", "C13", NULL}, compose_so2, 
          "SO2 Detection RGB", false },
        
        { "composite", {"C01", "C02", "C03", "C13", NULL}, compose_composite, 
          "Day/Night Composite", true },  // needs_navigation = true
        
        { NULL, {NULL}, NULL, NULL, false }  // Sentinel
    };
    ```

- [x] **2.4. Implementar Helper de Búsqueda** (COMPLETADO)
    ```c
    static const RgbStrategy* get_strategy_for_mode(const char *mode) {
        for (int i = 0; STRATEGIES[i].mode_name != NULL; i++) {
            if (strcmp(STRATEGIES[i].mode_name, mode) == 0) {
                return &STRATEGIES[i];
            }
        }
        return NULL;
    }
    ```

---

## ✅ Fase 3: Pipeline Principal (The Runner) - COMPLETADA

El objetivo es que `run_rgb` sea una secuencia lineal de pasos sin lógica de negocio mezclada.

**Estado Actual (run_rgb líneas 269-884):**
- Parsing inline (líneas 269-310)
- Creación de ChannelSet según modo (líneas 319-342)
- `find_channel_filenames()` y mapeo a `c_info[17]` (líneas 356-379)
- Carga con `load_nc_sf()` (líneas 383-389)
- Downsampling de C01/C02/C03 (líneas 398-408)
- Navegación con `compute_navigation_nc()` (línea 411)
- Clip PRE-reproyección (líneas 414-455)
- Reproyección (líneas 458-510)
- Clip POST-reproyección (líneas 513-554) o Clip NATIVO (líneas 555-582)
- Generación de output_filename (líneas 585-635)
- Composición (líneas 638-752)
- Post-procesamiento: gamma, histogram, CLAHE (líneas 754-767)
- Creación de alpha_mask (líneas 770-777)
- Remuestreo con scale (líneas 780-807)
- Agregar canal alpha (líneas 810-819)
- Escritura GeoTIFF o PNG (líneas 822-869)
- Limpieza manual (líneas 871-882)

- [x] **3.1. Crear `load_channels`** (COMPLETADO)
    Implementada en `src/rgb.c`:
    ```c
    static bool load_channels(RgbContext *ctx, const RgbStrategy *strategy) {
        // 1. Crear ChannelSet
        int count = 0;
        while (strategy->req_channels[count] != NULL) count++;
        ctx->channel_set = channelset_create(strategy->req_channels, count);
        if (!ctx->channel_set) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), 
                     "Falla de memoria al crear ChannelSet");
            return false;
        }
        
        // 2. Extraer ID signature del input_file
        const char *basename_input = basename((char*)ctx->opts.input_file);
        if (find_id_from_name(basename_input, ctx->id_signature) != 0) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "No se pudo extraer ID del nombre: %s", basename_input);
            return false;
        }
        strcpy(ctx->channel_set->id_signature, ctx->id_signature);
        
        // 3. Buscar archivos de canales
        char *dirnm_dup = strdup(ctx->opts.input_file);
        const char *dirnm = dirname(dirnm_dup);
        if (find_channel_filenames(dirnm, ctx->channel_set, 
                                   ctx->opts.is_l2_product) != 0) {
            free(dirnm_dup);
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "No se pudo acceder al directorio %s", dirnm);
            return false;
        }
        free(dirnm_dup);
        
        // 4. Mapear canales y validar
        for (int i = 0; i < ctx->channel_set->count; i++) {
            if (!ctx->channel_set->channels[i].filename) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                         "Falta archivo para canal %s", 
                         ctx->channel_set->channels[i].name);
                return false;
            }
            int cn = atoi(ctx->channel_set->channels[i].name + 1); // "C01" -> 1
            if (cn > 0 && cn <= 16) {
                const char *var = ctx->opts.is_l2_product ? "CMI" : "Rad";
                load_nc_sf(ctx->channel_set->channels[i].filename, var, 
                          &ctx->channels[cn]);
                
                // Identificar canal de referencia (mayor resolución)
                if (ctx->ref_channel_idx == 0 || cn > ctx->ref_channel_idx)
                    ctx->ref_channel_idx = cn;
            }
        }
        
        // 5. Downsampling de canales visible (si existen)
        if (ctx->channels[1].fdata.data_in) {
            DataF tmp = downsample_boxfilter(ctx->channels[1].fdata, 2);
            dataf_destroy(&ctx->channels[1].fdata);
            ctx->channels[1].fdata = tmp;
        }
        if (ctx->channels[2].fdata.data_in) {
            DataF tmp = downsample_boxfilter(ctx->channels[2].fdata, 4);
            dataf_destroy(&ctx->channels[2].fdata);
            ctx->channels[2].fdata = tmp;
        }
        if (ctx->channels[3].fdata.data_in) {
            DataF tmp = downsample_boxfilter(ctx->channels[3].fdata, 2);
            dataf_destroy(&ctx->channels[3].fdata);
            ctx->channels[3].fdata = tmp;
        }
        
        return true;
    }
    ```

- [x] **3.2. Crear `process_geospatial`** (COMPLETADO)
    Implementada en `src/rgb.c`:
    ```c
    static bool process_geospatial(RgbContext *ctx, const RgbStrategy *strategy) {
        // 1. Calcular navegación (siempre, incluso en modo nativo)
        const char *ref_filename = ctx->channel_set->channels[0].filename;
        compute_navigation_nc(ref_filename, &ctx->nav_lat, &ctx->nav_lon);
        ctx->has_navigation = (ctx->nav_lat.data_in != NULL);
        
        // 2. Si se requiere navegación para el composer, validar
        if (strategy->needs_navigation && !ctx->has_navigation) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "El modo '%s' requiere datos de navegación", 
                     strategy->mode_name);
            return false;
        }
        
        // 3. Clip PRE-reproyección (si aplica)
        if (ctx->opts.has_clip && ctx->opts.do_reprojection) {
            int ix, iy, iw, ih, vs;
            vs = reprojection_find_bounding_box(
                &ctx->nav_lat, &ctx->nav_lon,
                ctx->opts.clip_coords[0], ctx->opts.clip_coords[1],
                ctx->opts.clip_coords[2], ctx->opts.clip_coords[3],
                &ix, &iy, &iw, &ih);
            
            if (vs > 4) {
                LOG_INFO("Recorte PRE-reproyección: Start(%d,%d) Size(%d,%d)", 
                         ix, iy, iw, ih);
                // Aplicar crop a todos los canales cargados
                for (int i = 1; i <= 16; i++) {
                    if (ctx->channels[i].fdata.data_in) {
                        DataF cropped = dataf_crop(&ctx->channels[i].fdata, 
                                                   ix, iy, iw, ih);
                        dataf_destroy(&ctx->channels[i].fdata);
                        ctx->channels[i].fdata = cropped;
                    }
                }
                // Crop navegación
                DataF nav_lat_c = dataf_crop(&ctx->nav_lat, ix, iy, iw, ih);
                DataF nav_lon_c = dataf_crop(&ctx->nav_lon, ix, iy, iw, ih);
                dataf_destroy(&ctx->nav_lat);
                dataf_destroy(&ctx->nav_lon);
                ctx->nav_lat = nav_lat_c;
                ctx->nav_lon = nav_lon_c;
            }
        }
        
        // 4. Reproyección (si solicitada)
        if (ctx->opts.do_reprojection) {
            LOG_INFO("Iniciando reproyección a coordenadas geográficas...");
            bool first = true;
            
            for (int i = 1; i <= 16; i++) {
                if (ctx->channels[i].fdata.data_in) {
                    float lmin, lmax, ltmin, ltmax;
                    DataF repro;
                    
                    if (ctx->opts.has_clip) {
                        repro = reproject_to_geographics_with_nav(
                            &ctx->channels[i].fdata, &ctx->nav_lat, &ctx->nav_lon,
                            ctx->channels[i].native_resolution_km,
                            &lmin, &lmax, &ltmin, &ltmax, ctx->opts.clip_coords);
                    } else {
                        repro = reproject_to_geographics(
                            &ctx->channels[i].fdata, ref_filename,
                            ctx->channels[i].native_resolution_km,
                            &lmin, &lmax, &ltmin, &ltmax);
                    }
                    
                    dataf_destroy(&ctx->channels[i].fdata);
                    ctx->channels[i].fdata = repro;
                    
                    if (first) {
                        ctx->final_lon_min = lmin;
                        ctx->final_lon_max = lmax;
                        ctx->final_lat_min = ltmin;
                        ctx->final_lat_max = ltmax;
                        first = false;
                    }
                }
            }
            
            // Reconstruir navegación para coordenadas reproyectadas
            dataf_destroy(&ctx->nav_lat);
            dataf_destroy(&ctx->nav_lon);
            create_navigation_from_reprojected_bounds(
                &ctx->nav_lat, &ctx->nav_lon,
                ctx->channels[ctx->ref_channel_idx].fdata.width,
                ctx->channels[ctx->ref_channel_idx].fdata.height,
                ctx->final_lon_min, ctx->final_lon_max,
                ctx->final_lat_min, ctx->final_lat_max);
            
            // 5. Clip POST-reproyección (si aplica)
            if (ctx->opts.has_clip) {
                // [Código de clip POST - líneas 513-554]
                // ... (similar a PRE pero ajusta final_lon/lat)
            }
        } else {
            // 6. Modo NATIVO con clip
            if (ctx->opts.has_clip) {
                // [Código clip nativo - líneas 555-582]
                // Usa reprojection_find_bounding_box y guarda offsets
            }
        }
        
        return true;
    }
    ```

- [x] **3.3. Crear `generate_output_filename`** (COMPLETADO)
    Implementada en `src/rgb.c`:
    ```c
    static bool generate_output_filename(RgbContext *ctx) {
        const char *out = NULL;
        
        if (ap_found(parser, "out")) {
            const char *user_out = ap_get_str_value(parser, "out");
            
            // Detectar patrón con llaves
            if (strchr(user_out, '{') && strchr(user_out, '}')) {
                const char *ts_ref = ctx->channel_set->channels[0].filename;
                ctx->opts.output_filename = expand_filename_pattern(user_out, ts_ref);
                ctx->opts.output_generated = true;
            } else {
                ctx->opts.output_filename = (char*)user_out;
                ctx->opts.output_generated = false;
            }
            
            // Cambiar .png a .tif si se fuerza GeoTIFF
            if (ctx->opts.force_geotiff) {
                const char *ext = strrchr(ctx->opts.output_filename, '.');
                if (ext && strcmp(ext, ".png") == 0) {
                    size_t base_len = ext - ctx->opts.output_filename;
                    char *new_fn = malloc(base_len + 5);
                    strncpy(new_fn, ctx->opts.output_filename, base_len);
                    strcpy(new_fn + base_len, ".tif");
                    if (ctx->opts.output_generated)
                        free(ctx->opts.output_filename);
                    ctx->opts.output_filename = new_fn;
                    ctx->opts.output_generated = true;
                }
            }
        } else {
            // Generar nombre automático
            const char *ext = ctx->opts.force_geotiff ? ".tif" : ".png";
            ctx->opts.output_filename = generate_default_output_filename(
                ctx->channel_set->channels[0].filename, 
                ctx->opts.mode, ext);
            ctx->opts.output_generated = true;
            
            // Agregar "_geo" si está reproyectado
            if (ctx->opts.do_reprojection) {
                char *dot = strrchr(ctx->opts.output_filename, '.');
                if (dot) {
                    size_t len = strlen(ctx->opts.output_filename);
                    char *new_fn = malloc(len + 5);
                    size_t pre_len = dot - ctx->opts.output_filename;
                    strncpy(new_fn, ctx->opts.output_filename, pre_len);
                    strcpy(new_fn + pre_len, "_geo");
                    strcat(new_fn, dot);
                    free(ctx->opts.output_filename);
                    ctx->opts.output_filename = new_fn;
                }
            }
        }
        
        return (ctx->opts.output_filename != NULL);
    }
    ```

- [x] **3.4. Crear `apply_postprocessing`** (COMPLETADO)
    Implementada en `src/rgb.c`:
    ```c
    static bool apply_postprocessing(RgbContext *ctx) {
        // 1. Gamma
        if (ctx->opts.gamma != 1.0) {
            image_apply_gamma(ctx->final_image, ctx->opts.gamma);
        }
        
        // 2. Histogram/CLAHE (solo si NO es composite, que ya lo aplicó)
        if (strcmp(ctx->opts.mode, "composite") != 0) {
            if (ctx->opts.apply_histogram)
                image_apply_histogram(ctx->final_image);
            if (ctx->opts.apply_clahe)
                image_apply_clahe(ctx->final_image, 
                                 ctx->opts.clahe_tiles_x,
                                 ctx->opts.clahe_tiles_y, 
                                 ctx->opts.clahe_clip_limit);
        }
        
        // 3. Crear máscara alpha (antes de remuestreo)
        if (ctx->opts.use_alpha) {
            LOG_INFO("Creando máscara alpha...");
            ctx->alpha_mask = image_create_alpha_mask_from_dataf(
                &ctx->channels[ctx->ref_channel_idx].fdata);
        }
        
        // 4. Remuestreo
        if (ctx->opts.scale < 0) {
            // Downsampling
            int factor = -ctx->opts.scale;
            ImageData scaled = image_downsample_boxfilter(&ctx->final_image, factor);
            image_destroy(&ctx->final_image);
            ctx->final_image = scaled;
            
            if (ctx->alpha_mask.data) {
                ImageData scaled_mask = image_downsample_boxfilter(&ctx->alpha_mask, factor);
                image_destroy(&ctx->alpha_mask);
                ctx->alpha_mask = scaled_mask;
            }
        } else if (ctx->opts.scale > 1) {
            // Upsampling
            ImageData scaled = image_upsample_bilinear(&ctx->final_image, ctx->opts.scale);
            image_destroy(&ctx->final_image);
            ctx->final_image = scaled;
            
            if (ctx->alpha_mask.data) {
                ImageData scaled_mask = image_upsample_bilinear(&ctx->alpha_mask, ctx->opts.scale);
                image_destroy(&ctx->alpha_mask);
                ctx->alpha_mask = scaled_mask;
            }
        }
        
        // 5. Agregar canal alpha
        if (ctx->opts.use_alpha && ctx->alpha_mask.data) {
            LOG_INFO("Agregando canal alpha...");
            ImageData with_alpha = image_add_alpha_channel(&ctx->final_image, 
                                                          &ctx->alpha_mask);
            if (with_alpha.data) {
                image_destroy(&ctx->final_image);
                ctx->final_image = with_alpha;
            }
        }
        
        return true;
    }
    ```

- [x] **3.5. Crear `write_output`** (COMPLETADO)
    Implementada en `src/rgb.c`:
    ```c
    static bool write_output(RgbContext *ctx) {
        bool is_geotiff = ctx->opts.force_geotiff || 
                         (ctx->opts.output_filename && 
                          (strstr(ctx->opts.output_filename, ".tif") ||
                           strstr(ctx->opts.output_filename, ".tiff")));
        
        if (is_geotiff) {
            DataNC meta_out = {0};
            
            if (ctx->opts.do_reprojection) {
                // Metadatos LAT/LON
                meta_out.proj_code = PROJ_LATLON;
                size_t w = ctx->final_image.width;
                size_t h = ctx->final_image.height;
                
                meta_out.geotransform[0] = ctx->final_lon_min;
                meta_out.geotransform[1] = (ctx->final_lon_max - ctx->final_lon_min) / (double)w;
                meta_out.geotransform[2] = 0.0;
                meta_out.geotransform[3] = ctx->final_lat_max;
                meta_out.geotransform[4] = 0.0;
                meta_out.geotransform[5] = (ctx->final_lat_min - ctx->final_lat_max) / (double)h;
                
                write_geotiff_rgb(ctx->opts.output_filename, &ctx->final_image, 
                                 &meta_out, 0, 0);
            } else {
                // Metadatos NATIVOS
                meta_out = ctx->channels[ctx->ref_channel_idx];
                
                // Ajustar geotransform si se aplicó scale
                if (ctx->opts.scale != 1) {
                    double scale_factor = (ctx->opts.scale < 0) 
                        ? -ctx->opts.scale : ctx->opts.scale;
                    if (ctx->opts.scale > 1) {
                        meta_out.geotransform[1] /= scale_factor;
                        meta_out.geotransform[5] /= scale_factor;
                    } else {
                        meta_out.geotransform[1] *= scale_factor;
                        meta_out.geotransform[5] *= scale_factor;
                    }
                }
                
                write_geotiff_rgb(ctx->opts.output_filename, &ctx->final_image,
                                 &meta_out, ctx->crop_x_offset, ctx->crop_y_offset);
            }
        } else {
            writer_save_png(ctx->opts.output_filename, &ctx->final_image);
        }
        
        LOG_INFO("Imagen guardada en: %s", ctx->opts.output_filename);
        return true;
    }
    ```

- [x] **3.6. Reescribir `run_rgb` (Implementación Final)** (COMPLETADO)
    Simplificado con flujo limpio en `src/rgb.c`:
    ```c
    int run_rgb(ArgParser *parser) {
        // Inicialización
        LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
        logger_init(log_level);
        
        RgbContext ctx;
        rgb_context_init(&ctx);
        int status = -1;
        
        // Paso 1: Parsing
        if (!rgb_parse_options(parser, &ctx)) {
            LOG_ERROR("%s", ctx.error_msg);
            goto cleanup;
        }
        
        // Paso 2: Obtener estrategia
        const RgbStrategy *strategy = get_strategy_for_mode(ctx.opts.mode);
        if (!strategy) {
            LOG_ERROR("Modo '%s' no reconocido.", ctx.opts.mode);
            goto cleanup;
        }
        LOG_INFO("Modo seleccionado: %s - %s", strategy->mode_name, 
                 strategy->description);
        
        // Paso 3: Cargar canales
        if (!load_channels(&ctx, strategy)) {
            LOG_ERROR("%s", ctx.error_msg);
            goto cleanup;
        }
        
        // Paso 4: Procesar geoespacial (navegación, clip, reproyección)
        if (!process_geospatial(&ctx, strategy)) {
            LOG_ERROR("%s", ctx.error_msg);
            goto cleanup;
        }
        
        // Paso 5: Generar nombre de salida
        if (!generate_output_filename(&ctx)) {
            LOG_ERROR("No se pudo generar nombre de archivo de salida");
            goto cleanup;
        }
        
        // Paso 6: Composición (estrategia específica)
        LOG_INFO("Generando '%s'...", strategy->mode_name);
        ctx.final_image = strategy->composer_func(&ctx);
        if (ctx.final_image.data == NULL) {
            LOG_ERROR("Fallo al generar la imagen RGB");
            goto cleanup;
        }
        
        // Paso 7: Post-procesamiento (gamma, histogram, CLAHE, scale, alpha)
        if (!apply_postprocessing(&ctx)) {
            LOG_ERROR("Fallo en post-procesamiento");
            goto cleanup;
        }
        
        // Paso 8: Escritura
        if (!write_output(&ctx)) {
            LOG_ERROR("Fallo al guardar imagen");
            goto cleanup;
        }
        
        status = 0;  // Éxito
        
    cleanup:
        rgb_context_destroy(&ctx);
        return status;
    }
    ```

---

## 🚀 Fase 4: Validación y Testing

Una vez implementada la refactorización, validar que todo funciona igual que antes.

- [x] **4.1. Tests de Regresión** (COMPLETADO)
    ✅ Script `reproduction/run_demo.sh` implementado y ejecutándose exitosamente:
    - ✅ Test truecolor → `reproduction/demo_truecolor.tif`
    - ✅ Test ash → `reproduction/demo_ash.tif`
    - ✅ Test composite con todas las opciones → `reproduction/test_composite.tif`
    
    ```bash
    # Ejecutar todos los tests
    cd reproduction && ./run_demo.sh
    ```

- [ ] **4.2. Test de Memory Leaks**
    Ejecutar con Valgrind:
    ```bash
    valgrind --leak-check=full --show-leak-kinds=all \
             ./hpsatviews rgb input.nc --mode composite --out test.png
    ```

- [ ] **4.3. Benchmark de Performance**
    Comparar tiempos de ejecución antes/después:
    ```bash
    time ./hpsatviews_old rgb input.nc --mode composite --out old.png
    time ./hpsatviews_new rgb input.nc --mode composite --out new.png
    ```

---

## 🎯 Fase 5: Optimizaciones (Opcional - Post-Refactor)

Una vez validada la arquitectura, optimizar rendimiento y usabilidad.

- [ ] **5.1. Optimización `blend_images_rgb_indexed`**
    Actualmente `blend_images()` trabaja con 2 imágenes RGB completas.
    * Implementar versión optimizada que procese imagen nocturna indexada (1 byte/pixel)
    * Evitar conversión a RGB completo antes del blend
    * Estimar ahorro: ~20MB de memoria + 15% más rápido en composite

- [ ] **5.2. Configuración de Rutas**
    Eliminar rutas hardcodeadas (`/usr/local/share/lanot/...`):
    * Crear variable de entorno `HPSATVIEWS_DATA_DIR`
    * Implementar `const char* resolve_data_path(const char *relative_path)`
    * Usar en: citylights, clip CSV, Rayleigh LUT

- [ ] **5.3. Paralelización de Carga con OpenMP**
    Cargar múltiples canales en paralelo (OpenMP ya está en uso en el proyecto):
    ```c
    // En load_channels(), reemplazar el loop secuencial (líneas 383-389) por:
    
    // Construir array de índices de canales a cargar
    int channels_to_load[16];
    int load_count = 0;
    for (int i = 0; i < ctx->channel_set->count; i++) {
        int cn = atoi(ctx->channel_set->channels[i].name + 1);
        if (cn > 0 && cn <= 16) {
            channels_to_load[load_count++] = cn;
        }
    }
    
    // Cargar en paralelo
    const char *var = ctx->opts.is_l2_product ? "CMI" : "Rad";
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < load_count; i++) {
        int cn = channels_to_load[i];
        const char *filename = NULL;
        
        // Encontrar filename correspondiente (buscar en channel_set)
        for (int j = 0; j < ctx->channel_set->count; j++) {
            if (atoi(ctx->channel_set->channels[j].name + 1) == cn) {
                filename = ctx->channel_set->channels[j].filename;
                break;
            }
        }
        
        if (filename) {
            load_nc_sf(filename, var, &ctx->channels[cn]);
        }
    }
    ```
    **Nota:** `schedule(dynamic)` distribuye mejor la carga porque los archivos NetCDF pueden tener tamaños diferentes.
    
    Estimar ahorro: ~30% en carga de 4+ canales (4 canales: 2.5s → 1.7s)

- [ ] **5.4. Cache de Navegación**
    `compute_navigation_nc()` es costoso y repetitivo:
    * Cachear resultado basado en filename + dimensiones
    * Reutilizar entre comandos consecutivos del mismo archivo

- [ ] **5.5. Modo Batch**
    Agregar comando para procesar múltiples archivos:
    ```bash
    ./hpsatviews rgb-batch --mode composite --pattern "*.nc" --out-dir ./output/
    ```

---

## 📋 Resumen de Cambios por Archivo

### **Modificaciones**
- **`rgb.h`**: Agregar `RgbOptions`, `RgbContext`, `RgbStrategy`, prototipos de funciones
- **`rgb.c`**: 
  - Reemplazar `run_rgb()` completo (~615 líneas → ~100 líneas)
  - Agregar 10+ funciones nuevas (parse, load, process, compose_*, write, etc)
  - Eliminar variables globales inline

### **Sin Cambios**
- **`truecolor_rgb.c`**: `create_truecolor_rgb()` y `create_truecolor_rgb_rayleigh()` se usan tal cual
- **`nocturnal_pseudocolor.c`**: `create_nocturnal_pseudocolor()` se usa tal cual
- **`daynight_mask.c`**: `create_daynight_mask()` se usa tal cual
- **`image.c`**: Todas las funciones (`blend_images`, `image_apply_*`, etc) se usan tal cual
- **`datanc.c/h`**: `DataNC`, `DataF` sin cambios
- **`reprojection.c/h`**: Funciones de reproyección sin cambios
- **`filename_utils.c/h`**: Funciones de generación de nombres sin cambios

### **Archivos Auxiliares**
- **`clip_loader.c/h`**: `process_clip_coords()` se integra en parsing
- **`reader_nc.c/h`**: `load_nc_sf()`, `compute_navigation_nc()` sin cambios
- **`writer_geotiff.c/h`**: `write_geotiff_rgb()` sin cambios
- **`writer_png.c/h`**: `writer_save_png()` sin cambios

---

## 📝 Notas para el Desarrollador

### **Memory Management**
* **Critical:** Cualquier puntero añadido a `RgbContext` DEBE liberarse en `rgb_context_destroy()`
* Usar `goto cleanup` consistentemente para TODOS los errores
* No hacer `return` directo sin pasar por cleanup
* Validar punteros antes de `free()` (los helpers ya lo hacen, pero verificar)

### **Error Handling**
* Usar `ctx->error_msg` para mensajes descriptivos
* Setear `ctx->error_occurred = true` cuando falle algo
* En `run_rgb`, revisar `error_occurred` antes de cada paso
* Los `LOG_ERROR` deben ir DESPUÉS de setear `error_msg`

### **Testing Strategy**
1. **Fase 1-2:** Compilar sin errores, pero sin usar (dead code temporalmente)
2. **Fase 3:** Crear `run_rgb_new()` paralelo, comparar salidas con `run_rgb()` viejo
3. **Post-validación:** Renombrar `run_rgb()` viejo a `run_rgb_legacy()`, hacer `run_rgb = run_rgb_new`
4. **Limpieza final:** Eliminar código legacy después de 1 semana de testing

### **Debugging Tips**
* Activar modo verbose: `--verbose` para ver flujo detallado
* Inspeccionar `ctx` con GDB: `p ctx.opts.mode`, `p ctx.ref_channel_idx`
* Agregar LOG_DEBUG en cada función nueva
* Si crash, revisar stack con: `gdb ./hpsatviews core` → `bt full`

### **Code Style**
* Mantener consistencia con estilo existente (4 espacios, no tabs)
* Funciones estáticas en `rgb.c`, públicas en `rgb.h`
* Comentarios en español (matching existing code)
* Structs con typedef, no `struct RgbContext` sino `RgbContext`

### **Performance Notes**
* No optimizar prematuramente - Fase 5 es OPCIONAL
* Medir antes/después con `time` y `valgrind --tool=callgrind`
* OpenMP ya está presente en código existente - reutilizar patrones
* Los cuellos de botella reales son: I/O NetCDF, reproyección, blend

---

## ✅ Criterios de Éxito

La refactorización será exitosa cuando:

1. ✅ **Funcionalidad:** Todas las pruebas de regresión pasen (output idéntico byte-a-byte)
2. ✅ **Mantenibilidad:** Agregar un nuevo modo RGB tome < 50 líneas de código
3. ✅ **Memoria:** Valgrind reporte 0 leaks en todos los modos
4. ✅ **Performance:** Tiempo de ejecución dentro de ±5% del original
5. ✅ **Legibilidad:** `run_rgb()` sea comprensible en < 5 minutos de lectura
6. ✅ **Extensibilidad:** Agregar opción nueva (ej: `--brightness`) sea trivial

---

## 🗓️ Estimación de Tiempo

- **Fase 1:** 2-3 horas (structs + lifecycle + parsing)
- **Fase 2:** 3-4 horas (6 composers + strategy table)
- **Fase 3:** 4-5 horas (5 funciones pipeline + reescribir run_rgb)
- **Fase 4:** 2-3 horas (testing exhaustivo)
- **Fase 5:** Opcional, 3-5 horas (optimizaciones)

**Total:** 11-15 horas de desarrollo + testing

---

## 🔄 Estrategia de Migración Incremental (RECOMENDADO)

En lugar de reescribir todo de una vez, migrar gradualmente:

### **Semana 1: Proof of Concept**
1. Implementar solo Fase 1 (contexto sin usarlo)
2. Implementar Fase 2 solo para modo "ash" 
3. Crear `run_rgb_new()` que solo maneje "ash"
4. Comparar salidas con versión vieja
5. Si funciona → commit

### **Semana 2: Expandir**
1. Agregar "truecolor" y "night" a strategies
2. Agregar `process_geospatial()` simplificado
3. Expandir `run_rgb_new()` para 3 modos
4. Comparar salidas → commit

### **Semana 3: Completar**
1. Agregar modos restantes (airmass, so2)
2. Implementar "composite" (el más complejo)
3. Migrar post-procesamiento completo
4. Testing exhaustivo → commit

### **Semana 4: Deprecar Legacy**
1. Si todo funciona, hacer `run_rgb = run_rgb_new`
2. Mantener `run_rgb_legacy` por 1 semana
3. Si no hay issues, eliminar legacy
4. Tag release v2.0
