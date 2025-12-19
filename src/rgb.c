#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

#include "rgb.h"
#include "args.h"
#include "logger.h"
#include "clip_loader.h"
#include "filename_utils.h"
#include "truecolor.h"
#include "processing.h"
#include "image.h"
#include "datanc.h"
#include "reprojection.h"
#include "reader_nc.h"
#include "reader_png.h"
#include "writer_png.h"
#include "writer_geotiff.h"
#include "gray.h"
#include "nocturnal_pseudocolor.h"
#include "daynight_mask.h"


/**
 * @brief Inicializa el contexto RGB a sus valores por defecto.
 * Limpia la memoria y establece los valores predeterminados para las opciones.
 * @param ctx Puntero al contexto a inicializar.
 */
void rgb_context_init(RgbContext *ctx) {
    memset(ctx, 0, sizeof(RgbContext));
    // Inicializar defaults de opciones
    ctx->opts.gamma = 1.0f;
    ctx->opts.clahe_tiles_x = 8;
    ctx->opts.clahe_tiles_y = 8;
    ctx->opts.clahe_clip_limit = 4.0f;
    ctx->opts.scale = 1;
}

/**
 * @brief Libera toda la memoria dinámica contenida en el contexto RGB.
 * Es seguro llamar a esta función incluso si algunos punteros son NULL.
 * @param ctx Puntero al contexto a destruir.
 */
void rgb_context_destroy(RgbContext *ctx) {
    if (!ctx) return;

    // Liberar ChannelSet
    channelset_destroy(ctx->channel_set);
    
    // Liberar canales cargados
    for (int i = 1; i <= 16; i++) {
        // datanc_destroy es seguro para estructuras no inicializadas
        datanc_destroy(&ctx->channels[i]);
    }
    
    // Liberar navegación
    dataf_destroy(&ctx->nav_lat);
    dataf_destroy(&ctx->nav_lon);
    
    // Liberar resultados
    image_destroy(&ctx->final_image);
    image_destroy(&ctx->alpha_mask);
    
    // Liberar output_filename si fue generado dinámicamente
    if (ctx->opts.output_generated && ctx->opts.output_filename) {
        free(ctx->opts.output_filename);
    }
}

/**
 * @brief Parsea los argumentos de la línea de comandos y puebla la estructura RgbOptions.
 * Centraliza toda la lógica de parsing de argumentos para el comando 'rgb'.
 * @param parser El parser de argumentos inicializado.
 * @param ctx El contexto RGB donde se guardarán las opciones y los mensajes de error.
 * @return true si el parsing fue exitoso, false en caso de error.
 */
bool rgb_parse_options(ArgParser *parser, RgbContext *ctx) {
    // Validar archivo de entrada
    if (!ap_has_args(parser)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), 
                 "El comando 'rgb' requiere un archivo NetCDF de entrada.");
        ctx->error_occurred = true;
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
    ctx->opts.use_full_res = ap_found(parser, "full-res");
    
    // Parsear opciones con valores
    ctx->opts.mode = ap_get_str_value(parser, "mode");
    if (!ctx->opts.mode) {
        ctx->opts.mode = "composite"; // Valor por defecto
    }

    // ap_get_dbl_value y ap_get_int_value retornan 0 si no se encuentra la opción
    double gamma_val = ap_get_dbl_value(parser, "gamma");
    if (gamma_val != 0.0) ctx->opts.gamma = (float)gamma_val;
    
    int scale_val = ap_get_int_value(parser, "scale");
    if (ap_found(parser, "scale")) ctx->opts.scale = scale_val;
    
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
    
    // Parsear clip
    ctx->opts.has_clip = false;
    if (ap_found(parser, "clip")) {
        const char* clip_value = ap_get_str_value(parser, "clip");
        if (clip_value && strlen(clip_value) > 0) {
            // Intentar parsear como 4 coordenadas
            int parsed = sscanf(clip_value, "%f%*[, ]%f%*[, ]%f%*[, ]%f", 
                               &ctx->opts.clip_coords[0], &ctx->opts.clip_coords[1], 
                               &ctx->opts.clip_coords[2], &ctx->opts.clip_coords[3]);
            
            if (parsed == 4) {
                ctx->opts.has_clip = true;
                LOG_INFO("Usando recorte con coordenadas directas");
            } else {
                // Intentar cargar desde CSV
                GeoClip clip = buscar_clip_por_clave("/usr/local/share/lanot/docs/recortes_coordenadas.csv", clip_value);
                if (clip.encontrado) {
                    ctx->opts.clip_coords[0] = (float)clip.ul_x; // lon_min
                    ctx->opts.clip_coords[1] = (float)clip.ul_y; // lat_max
                    ctx->opts.clip_coords[2] = (float)clip.lr_x; // lon_max
                    ctx->opts.clip_coords[3] = (float)clip.lr_y; // lat_min
                    ctx->opts.has_clip = true;
                    LOG_INFO("Usando recorte '%s': %s", clip_value, clip.region);
                } else {
                    LOG_WARN("No se pudo cargar el recorte '%s'", clip_value);
                }
            }
        }
    }
    
    // Detectar producto L2
    // Es necesario duplicar el string porque basename puede modificarlo
    char *input_dup = strdup(ctx->opts.input_file);
    if (!input_dup) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Fallo de memoria al duplicar el nombre de archivo.");
        ctx->error_occurred = true;
        return false;
    }
    const char *basename_input = basename(input_dup);
    ctx->opts.is_l2_product = (strstr(basename_input, "CMIP") != NULL);
    free(input_dup);

    // Parsear nombre de salida
    if (ap_found(parser, "out")) {
        const char *user_out = ap_get_str_value(parser, "out");
        
        // Detectar patrones con llaves y expandirlos
        if (strchr(user_out, '{') && strchr(user_out, '}')) {
            ctx->opts.output_filename = expand_filename_pattern(user_out, ctx->opts.input_file);
            ctx->opts.output_generated = true; // Lo generamos nosotros
        } else {
            ctx->opts.output_filename = (char*)user_out;
            ctx->opts.output_generated = false; // El usuario lo proveyó exactamente así
        }
    }
    
    return true;
}

// --- FASE 2: COMPOSERS (PATRÓN ESTRATEGIA) ---

static ImageData compose_truecolor(RgbContext *ctx) {
    if (ctx->opts.apply_rayleigh) {
        // El filename de referencia para Rayleigh es el del canal 1
        const char *ref_filename = NULL;
        for (int i = 0; i < ctx->channel_set->count; i++) {
            if (strcmp(ctx->channel_set->channels[i].name, "C01") == 0) {
                ref_filename = ctx->channel_set->channels[i].filename;
                break;
            }
        }
        return create_truecolor_rgb_rayleigh(
            ctx->channels[1].fdata, 
            ctx->channels[2].fdata, 
            ctx->channels[3].fdata,
            ref_filename,
            true);
    } else {
        return create_truecolor_rgb(
            ctx->channels[1].fdata, 
            ctx->channels[2].fdata, 
            ctx->channels[3].fdata);
    }
}

static ImageData compose_night(RgbContext *ctx) {
    // Cargar imagen de fondo (luces de ciudad) si se solicita
    ImageData fondo_img = {0};
    const ImageData* fondo_ptr = NULL;
    if (ctx->opts.use_citylights) {
        const char* bg_path = (ctx->channels[ctx->ref_channel_idx].fdata.width == 2500)
                ? "/usr/local/share/lanot/images/land_lights_2012_conus.png"
                : "/usr/local/share/lanot/images/land_lights_2012_fd.png";
        LOG_INFO("Cargando imagen de fondo: %s", bg_path);
        fondo_img = reader_load_png(bg_path);
        if (fondo_img.data != NULL) {
            fondo_ptr = &fondo_img;
        } else {
            LOG_WARN("No se pudo cargar la imagen de fondo de luces de ciudad.");
        }
    }

    ImageData result = create_nocturnal_pseudocolor(&ctx->channels[13].fdata, fondo_ptr);
    image_destroy(&fondo_img); // Liberar la imagen de fondo si fue cargada
    return result;
}

static ImageData compose_ash(RgbContext *ctx) {
    // Buffers temporales locales, no en el contexto (diseño v3.1)
    DataF r_temp = {0}, g_temp = {0};

    r_temp = dataf_op_dataf(&ctx->channels[15].fdata, &ctx->channels[13].fdata, OP_SUB);
    g_temp = dataf_op_dataf(&ctx->channels[14].fdata, &ctx->channels[11].fdata, OP_SUB);
    
    ImageData result = create_multiband_rgb(&r_temp, &g_temp, &ctx->channels[13].fdata,
                                            -6.7f, 2.6f, -6.0f, 6.3f, 243.6f, 302.4f);
    
    dataf_destroy(&r_temp);
    dataf_destroy(&g_temp);
    return result;
}

static ImageData compose_airmass(RgbContext *ctx) {
    // Buffers temporales locales
    DataF r_temp = {0}, g_temp = {0}, b_temp = {0};

    r_temp = dataf_op_dataf(&ctx->channels[8].fdata, &ctx->channels[10].fdata, OP_SUB);
    g_temp = dataf_op_dataf(&ctx->channels[12].fdata, &ctx->channels[13].fdata, OP_SUB);
    b_temp = dataf_op_scalar(&ctx->channels[8].fdata, 273.15f, OP_SUB, true);

    ImageData result = create_multiband_rgb(&r_temp, &g_temp, &b_temp,
                                            -26.2f, 0.6f, -43.2f, 6.7f, 29.25f, 64.65f);

    dataf_destroy(&r_temp);
    dataf_destroy(&g_temp);
    dataf_destroy(&b_temp);
    return result;
}

static ImageData compose_so2(RgbContext *ctx) {
    // Buffers temporales locales
    DataF r_temp = {0}, g_temp = {0};

    r_temp = dataf_op_dataf(&ctx->channels[9].fdata, &ctx->channels[10].fdata, OP_SUB);
    g_temp = dataf_op_dataf(&ctx->channels[13].fdata, &ctx->channels[11].fdata, OP_SUB);

    ImageData result = create_multiband_rgb(&r_temp, &g_temp, &ctx->channels[13].fdata,
                                            -4.0f, 2.0f, -4.0f, 5.0f, 233.0f, 300.0f);

    dataf_destroy(&r_temp);
    dataf_destroy(&g_temp);
    return result;
}

static ImageData compose_composite(RgbContext *ctx) {
    // Genera imagen diurna (truecolor)
    ImageData diurna = compose_truecolor(ctx);
    
    // Aplicar histogram/CLAHE a la diurna ANTES del blend
    //if (ctx->opts.apply_histogram) image_apply_histogram(diurna);
    //if (ctx->opts.apply_clahe) {
    //    image_apply_clahe(diurna, ctx->opts.clahe_tiles_x, 
    //                      ctx->opts.clahe_tiles_y, ctx->opts.clahe_clip_limit);
    //}
    
    // Para el modo composite, forzamos el uso de citylights para la parte nocturna.
    // Guardamos el estado original de la opción para restaurarlo después.
    bool original_use_citylights = ctx->opts.use_citylights;
    ctx->opts.use_citylights = true;
    ImageData nocturna = compose_night(ctx);
    ctx->opts.use_citylights = original_use_citylights; // Restaurar estado original
    
    // La navegación ya está remuestreada al tamaño de referencia en process_geospatial,
    // así que podemos usarla directamente
    DataF *nav_lat_ptr = &ctx->nav_lat;
    DataF *nav_lon_ptr = &ctx->nav_lon;

    // Genera máscara día/noche usando los datos del contexto
    float day_night_ratio = 0.0f;
    ImageData mask = create_daynight_mask(
        ctx->channels[13], 
        *nav_lat_ptr, 
        *nav_lon_ptr, 
        &day_night_ratio, 
        263.15f);
    
    ImageData result = {0};
    // Si hay una porción significativa de noche (>15%), mezclamos las imágenes.
    // De lo contrario, usamos la imagen diurna directamente para ahorrar procesamiento.
    if (day_night_ratio > 0.15f && mask.data) {
        LOG_INFO("Mezclando imágenes diurna y nocturna (ratio día/noche: %.2f)", day_night_ratio);
        result = blend_images(nocturna, diurna, mask);
        image_destroy(&diurna); // La diurna ya no se necesita, blend_images crea una nueva
    } else {
        LOG_INFO("La escena es mayormente diurna, usando solo imagen diurna.");
        result = diurna; // Asignamos la imagen diurna directamente al resultado
    }
    
    // Liberar las imágenes temporales que ya no se necesitan
    image_destroy(&nocturna);
    image_destroy(&mask);
    return result;
}

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
    { NULL, {NULL}, NULL, NULL, false }  // Centinela
};

static const RgbStrategy* get_strategy_for_mode(const char *mode) {
    for (int i = 0; STRATEGIES[i].mode_name != NULL; i++) {
        if (strcmp(STRATEGIES[i].mode_name, mode) == 0) {
            return &STRATEGIES[i];
        }
    }
    return NULL;
}

// --- FASE 3: PIPELINE PRINCIPAL (THE RUNNER) ---

static bool load_channels(RgbContext *ctx, const RgbStrategy *strategy) {
    // 1. Crear ChannelSet
    int count = 0;
    while (strategy->req_channels[count] != NULL) count++;
    ctx->channel_set = channelset_create((const char**)strategy->req_channels, count);
    if (!ctx->channel_set) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falla de memoria al crear ChannelSet.");
        return false;
    }
    
    // 2. Extraer ID signature del input_file
    char *input_dup_id = strdup(ctx->opts.input_file);
    if (!input_dup_id) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Fallo de memoria al duplicar nombre de archivo.");
        return false;
    }
    const char *basename_input = basename(input_dup_id);
    if (find_id_from_name(basename_input, ctx->id_signature, sizeof(ctx->id_signature)) != 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No se pudo extraer ID del nombre: %s", basename_input);
        free(input_dup_id);
        return false;
    }
    strcpy(ctx->channel_set->id_signature, ctx->id_signature);
    free(input_dup_id);
    
    // 3. Buscar archivos de canales
    char *input_dup_dir = strdup(ctx->opts.input_file);
    if (!input_dup_dir) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Fallo de memoria al duplicar nombre de archivo.");
        return false;
    }
    const char *dirnm = dirname(input_dup_dir);
    if (find_channel_filenames(dirnm, ctx->channel_set, ctx->opts.is_l2_product) != 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No se pudo acceder al directorio o encontrar los canales en %s", dirnm);
        free(input_dup_dir);
        return false;
    }
    free(input_dup_dir);
    
    // 4. Cargar canales y validar
    LOG_INFO("Cargando canales requeridos...");
    for (int i = 0; i < ctx->channel_set->count; i++) {
        if (!ctx->channel_set->channels[i].filename) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falta archivo para canal %s", ctx->channel_set->channels[i].name);
            return false;
        }
        int cn = atoi(ctx->channel_set->channels[i].name + 1); // "C01" -> 1
        if (cn > 0 && cn <= 16) {
            const char *var = ctx->opts.is_l2_product ? "CMI" : "Rad";
            LOG_DEBUG("Cargando canal C%02d desde %s", cn, ctx->channel_set->channels[i].filename);
            if (load_nc_sf(ctx->channel_set->channels[i].filename, var, &ctx->channels[cn]) != 0) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Fallo al cargar NetCDF: %s", ctx->channel_set->channels[i].filename);
                return false;
            }
            
            // Identificar canal de referencia
            if (ctx->opts.use_full_res) {
                // Modo --full-res: buscar la MAYOR resolución (valor en km MÁS PEQUEÑO)
                if (ctx->ref_channel_idx == 0 || ctx->channels[cn].native_resolution_km < ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
                    ctx->ref_channel_idx = cn;
                }
            } else {
                // Modo por defecto: buscar la MENOR resolución (valor en km MÁS GRANDE)
                if (ctx->ref_channel_idx == 0 || ctx->channels[cn].native_resolution_km > ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
                    ctx->ref_channel_idx = cn;
                }
            }
        }
    }
    
    // Log de canales cargados para debug
    LOG_DEBUG("Canales cargados:");
    for (int i = 0; i < ctx->channel_set->count; i++) {
        int cn = atoi(ctx->channel_set->channels[i].name + 1);
        if (ctx->channels[cn].fdata.data_in) {
            LOG_DEBUG("  C%02d: %.1f km", cn, ctx->channels[cn].native_resolution_km);
        }
    }
    
    LOG_INFO("Canal de referencia: C%02d (%.1fkm)", ctx->ref_channel_idx, ctx->channels[ctx->ref_channel_idx].native_resolution_km);
    
    // Resample channels to match reference resolution
    float ref_res = ctx->channels[ctx->ref_channel_idx].native_resolution_km;
    for (int i = 0; i < ctx->channel_set->count; i++) {
        int cn = atoi(ctx->channel_set->channels[i].name + 1); // "C01" -> 1
        if (cn == ctx->ref_channel_idx || ctx->channels[cn].fdata.data_in == NULL) {
            continue; // No remuestrear el canal de referencia o canales vacíos
        }

        float res = ctx->channels[cn].native_resolution_km;
        float factor_f = res / ref_res;

        if (fabs(factor_f - 1.0f) > 0.01f) {
            int factor = (int)(factor_f + 0.5f);
            DataF resampled = {0};

            if (factor_f < 1.0f) { // La resolución de este canal es mayor que la de referencia -> Downsample
                factor = (int)((1.0f / factor_f) + 0.5f);
                LOG_INFO("Downsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res, factor);
                resampled = downsample_boxfilter(ctx->channels[cn].fdata, factor);
            } else { // La resolución de este canal es menor que la de referencia -> Upsample
                LOG_INFO("Upsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res, factor);
                resampled = upsample_bilinear(ctx->channels[cn].fdata, factor);
            }

            if (resampled.data_in) {
                dataf_destroy(&ctx->channels[cn].fdata);
                ctx->channels[cn].fdata = resampled;
            } else {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Fallo al remuestrear el canal C%02d", cn);
                for (int j = 0; j < i; j++) { // Limpiar los ya remuestreados en esta vuelta
                    dataf_destroy(&ctx->channels[atoi(ctx->channel_set->channels[j].name + 1)].fdata);
                }
                return false;
            }
        }
    }
    
    return true;
}

static bool process_geospatial(RgbContext *ctx, const RgbStrategy *strategy) {
    // 1. Calcular navegación (siempre, para GeoTIFF y/o reproyección)
    const char *ref_filename = ctx->channel_set->channels[0].filename;
    if (compute_navigation_nc(ref_filename, &ctx->nav_lat, &ctx->nav_lon) == 0) {
        ctx->has_navigation = true;
    } else {
        LOG_WARN("No se pudieron cargar los datos de navegación.");
        ctx->has_navigation = false;
    }
    
    // 2. Validar si la estrategia requiere navegación
    if (strategy->needs_navigation && !ctx->has_navigation) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "El modo '%s' requiere datos de navegación, pero no se pudieron cargar.", strategy->mode_name);
        return false;
    }
    
    // 3. Remuestrear navegación al tamaño del canal de referencia si es necesario
    if (ctx->has_navigation && ctx->ref_channel_idx > 0) {
        size_t nav_width = ctx->nav_lat.width;
        size_t ref_width = ctx->channels[ctx->ref_channel_idx].fdata.width;
        
        if (nav_width != ref_width) {
            if (nav_width > ref_width) {
                // Downsampling de navegación
                int factor = nav_width / ref_width;
                LOG_INFO("Remuestreando navegación al tamaño de referencia (factor downsample %d)", factor);
                DataF nav_lat_resampled = downsample_boxfilter(ctx->nav_lat, factor);
                DataF nav_lon_resampled = downsample_boxfilter(ctx->nav_lon, factor);
                
                if (nav_lat_resampled.data_in && nav_lon_resampled.data_in) {
                    dataf_destroy(&ctx->nav_lat);
                    dataf_destroy(&ctx->nav_lon);
                    ctx->nav_lat = nav_lat_resampled;
                    ctx->nav_lon = nav_lon_resampled;
                } else {
                    LOG_ERROR("Fallo al remuestrear la navegación");
                    return false;
                }
            } else {
                // Upsampling de navegación
                int factor = ref_width / nav_width;
                LOG_INFO("Remuestreando navegación al tamaño de referencia (factor upsample %d)", factor);
                DataF nav_lat_resampled = upsample_bilinear(ctx->nav_lat, factor);
                DataF nav_lon_resampled = upsample_bilinear(ctx->nav_lon, factor);
                
                if (nav_lat_resampled.data_in && nav_lon_resampled.data_in) {
                    dataf_destroy(&ctx->nav_lat);
                    dataf_destroy(&ctx->nav_lon);
                    ctx->nav_lat = nav_lat_resampled;
                    ctx->nav_lon = nav_lon_resampled;
                } else {
                    LOG_ERROR("Fallo al remuestrear la navegación");
                    return false;
                }
            }
        }
    }
    
    return true;
}

static bool generate_output_filename(RgbContext *ctx, const RgbStrategy *strategy) {
    if (ctx->opts.output_filename) { // El usuario ya proveyó un nombre
        // TODO: Manejar cambio de extensión si se fuerza GeoTIFF
        return true;
    }
    
    const char *ext = ctx->opts.force_geotiff ? ".tif" : ".png";
    char *base_filename = generate_default_output_filename(
        ctx->channel_set->channels[0].filename, 
        strategy->mode_name, ext);

    if (!base_filename) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Fallo al generar nombre de archivo de salida.");
        return false;
    }

    if (ctx->opts.do_reprojection) {
        char *dot = strrchr(base_filename, '.');
        if (dot) {
            size_t len = strlen(base_filename);
            char *new_fn = malloc(len + 5); // "_geo\0"
            if (!new_fn) { free(base_filename); return false; }
            
            size_t pre_len = dot - base_filename;
            strncpy(new_fn, base_filename, pre_len);
            strcpy(new_fn + pre_len, "_geo");
            strcat(new_fn, dot);
            
            free(base_filename);
            ctx->opts.output_filename = new_fn;
        } else {
            ctx->opts.output_filename = base_filename;
        }
    } else {
        ctx->opts.output_filename = base_filename;
    }
    
    ctx->opts.output_generated = true;
    return true;
}

static bool apply_postprocessing(RgbContext *ctx) {
    // 1. Gamma
    if (ctx->opts.gamma != 1.0f) {
        LOG_INFO("Aplicando corrección gamma: %.2f", ctx->opts.gamma);
        image_apply_gamma(ctx->final_image, ctx->opts.gamma);
    }
    
    // 2. Histogram/CLAHE (no para composite, que lo hace internamente)
    if (strcmp(ctx->opts.mode, "composite") != 0) {
        if (ctx->opts.apply_histogram) {
            LOG_INFO("Aplicando ecualización de histograma.");
            image_apply_histogram(ctx->final_image);
        }
        if (ctx->opts.apply_clahe) {
            LOG_INFO("Aplicando CLAHE (tiles=%dx%d, clip=%.1f)", ctx->opts.clahe_tiles_x, ctx->opts.clahe_tiles_y, ctx->opts.clahe_clip_limit);
            image_apply_clahe(ctx->final_image, ctx->opts.clahe_tiles_x, ctx->opts.clahe_tiles_y, ctx->opts.clahe_clip_limit);
        }
    }
    
    // 3. Crear máscara alpha (antes de remuestreo)
    if (ctx->opts.use_alpha) {
        LOG_INFO("Creando máscara alpha...");
        ctx->alpha_mask = image_create_alpha_mask_from_dataf(&ctx->channels[ctx->ref_channel_idx].fdata);
    }
    
    // 4. Remuestreo (scale)
    if (ctx->opts.scale != 1) {
        ImageData scaled_img = {0};
        if (ctx->opts.scale < 0) {
            LOG_INFO("Reduciendo imagen por factor %d", -ctx->opts.scale);
            scaled_img = image_downsample_boxfilter(&ctx->final_image, -ctx->opts.scale);
        } else { // scale > 1
            LOG_INFO("Ampliando imagen por factor %d", ctx->opts.scale);
            scaled_img = image_upsample_bilinear(&ctx->final_image, ctx->opts.scale);
        }
        image_destroy(&ctx->final_image);
        ctx->final_image = scaled_img;
    }
    
    // 5. Agregar canal alpha
    if (ctx->opts.use_alpha && ctx->alpha_mask.data) {
        LOG_INFO("Agregando canal alpha a la imagen final...");
        ImageData with_alpha = image_add_alpha_channel(&ctx->final_image, &ctx->alpha_mask);
        if (with_alpha.data) {
            image_destroy(&ctx->final_image);
            ctx->final_image = with_alpha;
        }
    }
    
    return true;
}

static bool write_output(RgbContext *ctx) {
    bool is_geotiff = ctx->opts.force_geotiff || 
                     (ctx->opts.output_filename && 
                      (strstr(ctx->opts.output_filename, ".tif") || strstr(ctx->opts.output_filename, ".tiff")));
    
    if (is_geotiff) {
        LOG_INFO("Guardando como GeoTIFF...");
        DataNC meta_out = {0};
        if (ctx->opts.do_reprojection) {
            // TODO: Metadatos para reproyección geográfica
        } else {
            // Metadatos nativos (geoestacionarios)
            meta_out = ctx->channels[ctx->ref_channel_idx];
            // TODO: Ajustar geotransform si hay scale o crop
        }
        write_geotiff_rgb(ctx->opts.output_filename, &ctx->final_image, &meta_out, ctx->crop_x_offset, ctx->crop_y_offset);
    } else {
        LOG_INFO("Guardando como PNG...");
        writer_save_png(ctx->opts.output_filename, &ctx->final_image);
    }
    
    LOG_INFO("Imagen guardada en: %s", ctx->opts.output_filename);
    return true;
}


int run_rgb(ArgParser *parser) {
    RgbContext ctx;
    rgb_context_init(&ctx);
    int status = 1; // Error por defecto

    // Paso 1: Parsear opciones
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
    LOG_INFO("Modo seleccionado: %s - %s", strategy->mode_name, strategy->description);

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
    if (!generate_output_filename(&ctx, strategy)) {
        LOG_ERROR("No se pudo generar el nombre de archivo de salida.");
        goto cleanup;
    }

    // Paso 6: Composición (corazón de la estrategia)
    LOG_INFO("Generando imagen '%s'...", strategy->mode_name);
    ctx.final_image = strategy->composer_func(&ctx);
    if (ctx.final_image.data == NULL) {
        LOG_ERROR("Fallo al generar la imagen RGB para el modo '%s'.", strategy->mode_name);
        goto cleanup;
    }

    // Paso 6.5: Reproyección (si fue solicitada)
    if (ctx.opts.do_reprojection) {
        if (!ctx.has_navigation) {
            LOG_ERROR("La reproyección fue solicitada pero no se pudo cargar la navegación.");
            goto cleanup;
        }
        
        LOG_INFO("Iniciando reproyección de imagen RGB a coordenadas geográficas...");
        ImageData reprojected = reproject_image_to_geographics(
            &ctx.final_image, &ctx.nav_lat, &ctx.nav_lon, 
            ctx.channels[ctx.ref_channel_idx].native_resolution_km,
            ctx.opts.has_clip ? ctx.opts.clip_coords : NULL
        );
        
        if (reprojected.data == NULL) {
            LOG_ERROR("Fallo durante la reproyección de la imagen RGB.");
            goto cleanup;
        }
        
        image_destroy(&ctx.final_image);
        ctx.final_image = reprojected;
        
        // Actualizar límites geográficos para GeoTIFF
        if (ctx.opts.has_clip) {
            ctx.final_lon_min = ctx.opts.clip_coords[0];
            ctx.final_lat_max = ctx.opts.clip_coords[1];
            ctx.final_lon_max = ctx.opts.clip_coords[2];
            ctx.final_lat_min = ctx.opts.clip_coords[3];
        } else {
            ctx.final_lon_min = ctx.nav_lon.fmin;
            ctx.final_lon_max = ctx.nav_lon.fmax;
            ctx.final_lat_min = ctx.nav_lat.fmin;
            ctx.final_lat_max = ctx.nav_lat.fmax;
        }
    }

    // Paso 7: Post-procesamiento (gamma, histogram, CLAHE, scale, alpha)
    if (!apply_postprocessing(&ctx)) {
        LOG_ERROR("Fallo en la etapa de post-procesamiento.");
        goto cleanup;
    }

    // Paso 8: Escritura
    if (!write_output(&ctx)) {
        LOG_ERROR("Fallo al guardar la imagen de salida.");
        goto cleanup;
    }

    status = 0; // ¡Éxito!

cleanup:
    rgb_context_destroy(&ctx);
    return status;
}