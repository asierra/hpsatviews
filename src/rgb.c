#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "clip_loader.h"
#include "config.h"
#include "datanc.h"
#include "daynight_mask.h"
#include "filename_utils.h"
#include "image.h"
#include "logger.h"
#include "metadata.h"
#include "nocturnal_pseudocolor.h"
#include "parse_expr.h"
#include "processing.h"
#include "rayleigh.h"
#include "reader_nc.h"
#include "reader_png.h"
#include "reprojection.h"
#include "rgb.h"
#include "truecolor.h"
#include "writer_geotiff.h"
#include "writer_png.h"

void rgb_context_init(RgbContext *ctx) {
    memset(ctx, 0, sizeof(RgbContext));
    // Inicializar defaults de opciones
    ctx->opts.gamma = 1.0f;
    ctx->opts.clahe_tiles_x = 8;
    ctx->opts.clahe_tiles_y = 8;
    ctx->opts.clahe_clip_limit = 4.0f;
    ctx->opts.scale = 1;
}

void rgb_context_destroy(RgbContext *ctx) {
    if (!ctx)
        return;

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

bool rgb_parse_options(ArgParser *parser, RgbContext *ctx) {
    // Validar archivo de entrada
    if (!ap_has_args(parser)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Se requiere un archivo NetCDF de entrada.");
        ctx->error_occurred = true;
        return false;
    }
    ctx->opts.input_file = ap_get_arg_at_index(parser, 0);

    // Parsear opciones booleanas
    ctx->opts.do_reprojection = ap_found(parser, "geographics");
    ctx->opts.apply_histogram = ap_found(parser, "histo");
    ctx->opts.force_geotiff = ap_found(parser, "geotiff");
    ctx->opts.apply_rayleigh = ap_found(parser, "rayleigh");
    ctx->opts.rayleigh_analytic = ap_found(parser, "ray-analytic");
    ctx->opts.use_citylights = ap_found(parser, "citylights");
    ctx->opts.use_alpha = ap_found(parser, "alpha");
    ctx->opts.use_full_res = ap_found(parser, "full-res");

    // Parsear opciones con valores
    ctx->opts.mode = ap_get_str_value(parser, "mode");
    if (!ctx->opts.mode) {
        ctx->opts.mode = "daynite"; // Valor por defecto
    }

    // ap_get_dbl_value y ap_get_int_value retornan 0 si no se encuentra la opción
    double gamma_val = ap_get_dbl_value(parser, "gamma");
    if (gamma_val != 0.0)
        ctx->opts.gamma = (float)gamma_val;

    int scale_val = ap_get_int_value(parser, "scale");
    if (ap_found(parser, "scale"))
        ctx->opts.scale = scale_val;

    // Parsear CLAHE
    ctx->opts.apply_clahe = ap_found(parser, "clahe") || ap_found(parser, "clahe-params");
    if (ctx->opts.apply_clahe) {
        const char *clahe_params = ap_get_str_value(parser, "clahe-params");
        sscanf(clahe_params, "%d,%d,%f", &ctx->opts.clahe_tiles_x, &ctx->opts.clahe_tiles_y,
               &ctx->opts.clahe_clip_limit);
    }

    // Parsear clip
    ctx->opts.has_clip = false;
    if (ap_found(parser, "clip")) {
        const char *clip_value = ap_get_str_value(parser, "clip");
        if (clip_value && strlen(clip_value) > 0) {
            // Intentar parsear como 4 coordenadas
            int parsed = sscanf(clip_value, "%f%*[, ]%f%*[, ]%f%*[, ]%f", &ctx->opts.clip_coords[0],
                                &ctx->opts.clip_coords[1], &ctx->opts.clip_coords[2],
                                &ctx->opts.clip_coords[3]);

            if (parsed == 4) {
                ctx->opts.has_clip = true;
                LOG_INFO("Usando recorte con coordenadas directas");
            } else {
                // Intentar cargar desde CSV
                GeoClip clip = buscar_clip_por_clave(
                    "/usr/local/share/lanot/docs/recortes_coordenadas.csv", clip_value);
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

    // Custom
    ctx->opts.expr = ap_get_str_value(parser, "expr");
    ctx->opts.minmax = ap_get_str_value(parser, "minmax");

    // Detectar producto L2
    // Es necesario duplicar el string porque basename puede modificarlo
    char *input_dup = strdup(ctx->opts.input_file);
    if (!input_dup) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Falla de memoria al duplicar el nombre de archivo.");
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
            ctx->opts.output_filename = (char *)user_out;
            ctx->opts.output_generated = false; // El usuario lo proveyó exactamente así
        }
    }

    return true;
}

// --- FASE 2: COMPOSERS (PATRÓN ESTRATEGIA) ---
extern void apply_solar_zenith_correction(DataF *data, const DataF *sza);

static bool compose_truecolor(RgbContext *ctx) {
    // 1. Setup y Copia
    DataF *ch_blue = &ctx->channels[1].fdata; // C01
    DataF *ch_red = &ctx->channels[2].fdata;  // C02
    DataF *ch_nir = &ctx->channels[3].fdata;  // C03

    ctx->comp_b = dataf_copy(ch_blue);
    ctx->comp_r = dataf_copy(ch_red);
    if (!ctx->comp_b.data_in || !ctx->comp_r.data_in)
        return false;

    // 2. CORRECCIÓN RAYLEIGH
    if (ctx->opts.apply_rayleigh || ctx->opts.rayleigh_analytic) {
        // Encontrar el archivo correcto para navegación
        const char *nav_file = NULL;
        for (int i = 0; i < ctx->channel_set->count; i++) {
            if (strcmp(ctx->channel_set->channels[i].name, "C01") == 0) {
                nav_file = ctx->channel_set->channels[i].filename;
                break;
            }
        }
        RayleighNav nav = {0};
        // Cargar navegación ajustada al tamaño de la imagen azul (C01)
        if (rayleigh_load_navigation(nav_file, &nav, ctx->comp_b.width, ctx->comp_b.height)) {
            LOG_INFO("Aplicando corrección solar zenith...");
            apply_solar_zenith_correction(&ctx->comp_b, &nav.sza);
            apply_solar_zenith_correction(&ctx->comp_r, &nav.sza);
            apply_solar_zenith_correction(ch_nir, &nav.sza);
            if (ctx->opts.rayleigh_analytic) {
                LOG_INFO("Aplicando Rayleigh Analítico...");
                analytic_rayleigh_correction(&ctx->comp_b, &nav, 0.47);
                analytic_rayleigh_correction(&ctx->comp_r, &nav, 0.64);
            } else {
                LOG_INFO("Aplicando Rayleigh Luts...");
                luts_rayleigh_correction(&ctx->comp_b, &nav, 1, RAYLEIGH_TAU_BLUE);
                luts_rayleigh_correction(&ctx->comp_r, &nav, 2, RAYLEIGH_TAU_RED);
            }
            rayleigh_free_navigation(&nav);
        } else {
            LOG_WARN("Falló carga de navegación, saltando Rayleigh.");
        }
    }
    // 3. Generar Verde
    ctx->comp_g = create_truecolor_synthetic_green(&ctx->comp_b, &ctx->comp_r, ch_nir);
    if (!ctx->comp_g.data_in)
        return false;
    // Resalte adicional del verde
    ctx->comp_g = dataf_op_scalar(&ctx->comp_g, 1.05f, OP_MUL, false);

    // 4. Rangos
    ctx->min_r = 0.0f;
    ctx->max_r = 1.1f;
    ctx->min_g = 0.0f;
    ctx->max_g = 1.1f;
    ctx->min_b = 0.0f;
    ctx->max_b = 1.1f;

    return true;
}

static bool compose_night(RgbContext *ctx) {
    // Cargar imagen de fondo (luces de ciudad) si se solicita
    ImageData fondo_img = {0};
    const ImageData *fondo_ptr = NULL;
    if (ctx->opts.use_citylights) {
        const char *bg_path = (ctx->channels[ctx->ref_channel_idx].fdata.width == 2500)
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
    ctx->final_image = create_nocturnal_pseudocolor(&ctx->channels[13].fdata, fondo_ptr);
    image_destroy(&fondo_img); // Liberar la imagen de fondo si fue cargada*/
    return true;
}

static bool compose_ash(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[15].fdata, &ctx->channels[13].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[14].fdata, &ctx->channels[11].fdata, OP_SUB);
    ctx->comp_b = ctx->channels[13].fdata;
    ctx->min_r = -6.7f;
    ctx->max_r = 2.6f;
    ctx->min_g = -6.0f;
    ctx->max_g = 6.3f;
    ctx->min_b = 243.6f;
    ctx->max_b = 302.4f;
    return true;
}

static bool compose_airmass(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[8].fdata, &ctx->channels[10].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[12].fdata, &ctx->channels[13].fdata, OP_SUB);
    ctx->comp_b = dataf_op_scalar(&ctx->channels[8].fdata, 273.15f, OP_SUB, true);
    ctx->min_r = -26.2f;
    ctx->max_r = 0.6f;
    ctx->min_g = -43.2f;
    ctx->max_g = 6.7f;
    ctx->min_b = 29.25f;
    ctx->max_b = 64.65f;
    return true;
}

static bool compose_so2(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[9].fdata, &ctx->channels[10].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[13].fdata, &ctx->channels[11].fdata, OP_SUB);
    ctx->comp_b = ctx->channels[13].fdata;
    ctx->min_r = -4.0f;
    ctx->max_r = 2.0f;
    ctx->min_g = -4.0f;
    ctx->max_g = 5.0f;
    ctx->min_b = 233.0f;
    ctx->max_b = 300.0f;
    return true;
}

static bool compose_daynite(RgbContext *ctx) {
    // Genera imagen diurna
    ctx->opts.apply_rayleigh = true;
    ctx->opts.gamma = 2.2f;
    if (!compose_truecolor(ctx)) {
        return false;
    }
    // Forzamos el uso de citylights para la parte nocturna.
    ctx->opts.use_citylights = true;
    if (!compose_night(ctx)) {
        return false;
    }
    ctx->alpha_mask = ctx->final_image;

    return true;
}

static bool compose_custom(RgbContext *ctx) {
    LOG_INFO("Armando RGB custom con expresión: %s", ctx->opts.expr);
    LinearCombo combo[3];
    memset(combo, 0, sizeof(combo));
    float ranges[3][2] = {{0, 255}, {0, 255}, {0, 255}}; // [min, max]

    // 1. Parsear expresiones (R;G;B)
    // Usamos una copia de la cadena porque strtok modifica el original
    char *expr_copy = strdup(ctx->opts.expr);
    if (!expr_copy) {
        LOG_ERROR("Falla de memoria al duplicar expresión.");
        return false;
    }
    char *token = strtok(expr_copy, ";");
    bool parse_error = false;
    for (int i = 0; i < 3; i++) {
        if (token == NULL) {
            LOG_ERROR("Error, deben ser 3 expresiones divididas por ';'.");
            parse_error = true;
            break;
        }
        if (parse_expr_string(token, &combo[i]) != 0) {
            LOG_ERROR("Error parseando expresión componente %d", i);
            parse_error = true;
        }
        token = strtok(NULL, ";");
    }
    free(expr_copy);
    if (parse_error)
        return false;

    // 2. Parsear rangos minmax (min,max; min,max; min,max) si los hay
    if (ctx->opts.minmax) {
        char *minmax_copy = strdup(ctx->opts.minmax);
        if (!minmax_copy) {
            LOG_ERROR("Falla de memoria al duplicar minmax.");
            return false;
        }
        char *m_token = strtok(minmax_copy, ";");
        for (int i = 0; i < 3 && m_token != NULL; i++) {
            if (sscanf(m_token, "%f,%f", &ranges[i][0], &ranges[i][1]) != 2) {
                LOG_WARN("No se pudieron leer los rangos para el componente %d: %s", i, m_token);
            }
            m_token = strtok(NULL, ";");
        }
        free(minmax_copy);
    }
    LOG_INFO("Rangos custom RGB: %s: %f,%f  %f,%f %f,%f", ctx->opts.minmax, ranges[0][0],
             ranges[0][1], ranges[1][0], ranges[1][1], ranges[2][0], ranges[2][1]);

    // 3. Evaluar las combinaciones lineales
    ctx->comp_r = evaluate_linear_combo(&combo[0], ctx->channels);
    ctx->comp_g = evaluate_linear_combo(&combo[1], ctx->channels);
    ctx->comp_b = evaluate_linear_combo(&combo[2], ctx->channels);

    if (!ctx->comp_r.data_in || !ctx->comp_g.data_in || !ctx->comp_b.data_in) {
        LOG_ERROR("Falla al evaluar las fórmulas matemáticas del modo custom.");
        return false;
    }

    // 4. Asignar rangos
    ctx->min_r = ranges[0][0];
    ctx->max_r = ranges[0][1];
    ctx->min_g = ranges[1][0];
    ctx->max_g = ranges[1][1];
    ctx->min_b = ranges[2][0];
    ctx->max_b = ranges[2][1];

    return true;
}

static const RgbStrategy STRATEGIES[] = {
    {"truecolor",
     {"C01", "C02", "C03", NULL},
     compose_truecolor,
     "True Color RGB (natural)",
     false},
    {"night", {"C13", NULL}, compose_night, "Nocturnal IR with temperature pseudocolor", false},
    {"ash", {"C11", "C13", "C14", "C15", NULL}, compose_ash, "Volcanic Ash RGB", false},
    {"airmass", {"C08", "C10", "C12", "C13", NULL}, compose_airmass, "Air Mass RGB", false},
    {"so2", {"C09", "C10", "C11", "C13", NULL}, compose_so2, "SO2 Detection RGB", false},
    {"daynite", {"C01", "C02", "C03", "C13", NULL}, compose_daynite, "Day/Night Composite", true},
    {"custom", {NULL}, compose_custom, "Custom mode", false},
    {NULL, {NULL}, NULL, NULL, false} // Centinela
};

static const RgbStrategy *get_strategy_for_mode(const char *mode) {
    if (strcmp("default", mode) == 0)
        return &STRATEGIES[5];
    for (int i = 0; STRATEGIES[i].mode_name != NULL; i++) {
        if (strcmp(STRATEGIES[i].mode_name, mode) == 0) {
            return &STRATEGIES[i];
        }
    }
    return NULL;
}

// --- FASE 3: PIPELINE PRINCIPAL (THE RUNNER) ---

static bool load_channels(RgbContext *ctx, const char **req_channels) {
    // 1. Crear ChannelSet
    int count = 0;
    while (req_channels[count] != NULL)
        count++;
    ctx->channel_set = channelset_create((const char **)req_channels, count);
    if (!ctx->channel_set) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falla de memoria al crear ChannelSet.");
        return false;
    }

    // 2. Extraer ID signature del input_file
    char *input_dup_id = strdup(ctx->opts.input_file);
    if (!input_dup_id) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Falla de memoria al duplicar nombre de archivo.");
        return false;
    }
    const char *basename_input = basename(input_dup_id);
    if (find_id_from_name(basename_input, ctx->id_signature, sizeof(ctx->id_signature)) != 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No se pudo extraer ID del nombre: %s",
                 basename_input);
        free(input_dup_id);
        return false;
    }
    strcpy(ctx->channel_set->id_signature, ctx->id_signature);
    free(input_dup_id);

    // 3. Buscar archivos de canales
    char *input_dup_dir = strdup(ctx->opts.input_file);
    if (!input_dup_dir) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Falla de memoria al duplicar nombre de archivo.");
        return false;
    }
    const char *dirnm = dirname(input_dup_dir);
    if (find_channel_filenames(dirnm, ctx->channel_set, ctx->opts.is_l2_product) != 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "No se pudo acceder al directorio o encontrar los canales en %s", dirnm);
        free(input_dup_dir);
        return false;
    }
    free(input_dup_dir);

    // 4. Cargar canales y validar
    LOG_INFO("Cargando canales requeridos...");
    for (int i = 0; i < ctx->channel_set->count; i++) {
        if (!ctx->channel_set->channels[i].filename) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falta archivo para canal %s",
                     ctx->channel_set->channels[i].name);
            return false;
        }
        int cn = atoi(ctx->channel_set->channels[i].name + 1); // "C01" -> 1
        if (cn > 0 && cn <= 16) {
            LOG_DEBUG("Cargando canal C%02d desde %s", cn, ctx->channel_set->channels[i].filename);
            if (load_nc_sf(ctx->channel_set->channels[i].filename, &ctx->channels[cn]) != 0) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falla al cargar NetCDF: %s",
                         ctx->channel_set->channels[i].filename);
                return false;
            }

            // Identificar canal de referencia
            if (ctx->opts.use_full_res) {
                // Modo --full-res: buscar la MAYOR resolución (valor en km MÁS PEQUEÑO)
                if (ctx->ref_channel_idx == 0 ||
                    ctx->channels[cn].native_resolution_km <
                        ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
                    ctx->ref_channel_idx = cn;
                }
            } else {
                // Modo por defecto: buscar la MENOR resolución (valor en km MÁS GRANDE)
                if (ctx->ref_channel_idx == 0 ||
                    ctx->channels[cn].native_resolution_km >
                        ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
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

    LOG_INFO("Canal de referencia: C%02d (%.1fkm)", ctx->ref_channel_idx,
             ctx->channels[ctx->ref_channel_idx].native_resolution_km);

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

            if (factor_f < 1.0f) { // La resolución de este canal es mayor que la de
                                   // referencia -> Downsample
                factor = (int)((1.0f / factor_f) + 0.5f);
                LOG_INFO("Downsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res,
                         factor);
                resampled = downsample_boxfilter(ctx->channels[cn].fdata, factor);
            } else { // La resolución de este canal es menor que la de referencia ->
                     // Upsample
                LOG_INFO("Upsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res,
                         factor);
                resampled = upsample_bilinear(ctx->channels[cn].fdata, factor);
            }

            if (resampled.data_in) {
                dataf_destroy(&ctx->channels[cn].fdata);
                ctx->channels[cn].fdata = resampled;
            } else {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                         "Falla al remuestrear el canal C%02d", cn);
                for (int j = 0; j < i; j++) { // Limpiar los ya remuestreados en esta vuelta
                    dataf_destroy(
                        &ctx->channels[atoi(ctx->channel_set->channels[j].name + 1)].fdata);
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
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "El modo '%s' requiere datos de navegación, pero no se pudieron "
                 "cargar.",
                 strategy->mode_name);
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
                LOG_INFO("Remuestreando navegación al tamaño de referencia (factor "
                         "downsample %d)",
                         factor);
                DataF nav_lat_resampled = downsample_boxfilter(ctx->nav_lat, factor);
                DataF nav_lon_resampled = downsample_boxfilter(ctx->nav_lon, factor);

                if (nav_lat_resampled.data_in && nav_lon_resampled.data_in) {
                    dataf_destroy(&ctx->nav_lat);
                    dataf_destroy(&ctx->nav_lon);
                    ctx->nav_lat = nav_lat_resampled;
                    ctx->nav_lon = nav_lon_resampled;
                } else {
                    LOG_ERROR("Falla al remuestrear la navegación");
                    return false;
                }
            } else {
                // Upsampling de navegación
                int factor = ref_width / nav_width;
                LOG_INFO("Remuestreando navegación al tamaño de referencia (factor "
                         "upsample %d)",
                         factor);
                DataF nav_lat_resampled = upsample_bilinear(ctx->nav_lat, factor);
                DataF nav_lon_resampled = upsample_bilinear(ctx->nav_lon, factor);

                if (nav_lat_resampled.data_in && nav_lon_resampled.data_in) {
                    dataf_destroy(&ctx->nav_lat);
                    dataf_destroy(&ctx->nav_lon);
                    ctx->nav_lat = nav_lat_resampled;
                    ctx->nav_lon = nav_lon_resampled;
                } else {
                    LOG_ERROR("Falla al remuestrear la navegación");
                    return false;
                }
            }
        }
    }

    return true;
}

static bool apply_postprocessing(RgbContext *ctx) {
    // daynite es un caso exclusivo especial
    if (strcmp(ctx->opts.mode, "daynite") == 0) {
        // La navegación ya está remuestreada al tamaño de referencia en
        // process_geospatial, así que podemos usarla directamente
        DataF *nav_lat_ptr = &ctx->nav_lat;
        DataF *nav_lon_ptr = &ctx->nav_lon;

        // Genera máscara día/noche usando los datos del contexto
        float day_night_ratio = 0.0f;
        ImageData mask = create_daynight_mask(ctx->channels[13], *nav_lat_ptr, *nav_lon_ptr,
                                              &day_night_ratio, 263.15f);

        // Si hay una porción significativa de noche (>15%), mezclamos las imágenes,
        // de lo contrario, usamos la imagen diurna directamente.
        if (day_night_ratio > 0.15f && mask.data) {
            LOG_INFO("Mezclando imágenes diurna y nocturna (ratio día/noche: %.2f)",
                     day_night_ratio);
            ctx->final_image = blend_images(ctx->alpha_mask, ctx->final_image, mask);
        } else {
            LOG_INFO("La escena es mayormente diurna, usando solo imagen diurna.");
            // Ya está en ctx->final_image
        }
        image_destroy(&ctx->alpha_mask);
        image_destroy(&mask);
    }
    // 1. Gamma solo se aplica a DataF

    // 2. Histogram/CLAHE (no para daynite)
    if (strcmp(ctx->opts.mode, "daynite") != 0) {
        if (ctx->opts.apply_histogram) {
            LOG_INFO("Aplicando ecualización de histograma.");
            image_apply_histogram(ctx->final_image);
        }
        if (ctx->opts.apply_clahe) {
            LOG_INFO("Aplicando CLAHE (tiles=%dx%d, clip=%.1f)", ctx->opts.clahe_tiles_x,
                     ctx->opts.clahe_tiles_y, ctx->opts.clahe_clip_limit);
            image_apply_clahe(ctx->final_image, ctx->opts.clahe_tiles_x, ctx->opts.clahe_tiles_y,
                              ctx->opts.clahe_clip_limit);
        }
    }

    // 3. Crear máscara alpha (antes de remuestreo)
    if (ctx->opts.use_alpha) {
        LOG_INFO("Creando máscara alpha...");
        ctx->alpha_mask =
            image_create_alpha_mask_from_dataf(&ctx->channels[ctx->ref_channel_idx].fdata);
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
                      (ctx->opts.output_filename && (strstr(ctx->opts.output_filename, ".tif") ||
                                                     strstr(ctx->opts.output_filename, ".tiff")));

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
        write_geotiff_rgb(ctx->opts.output_filename, &ctx->final_image, &meta_out,
                          ctx->crop_x_offset, ctx->crop_y_offset);
    } else {
        LOG_INFO("Guardando como PNG... %s", ctx->opts.output_filename);
        writer_save_png(ctx->opts.output_filename, &ctx->final_image);
    }

    LOG_INFO("Imagen guardada en: %s", ctx->opts.output_filename);
    return true;
}

// ============================================================================
// INTERFAZ UNIFICADA - Inyección de Dependencias
// ============================================================================

/**
 * @brief Convierte ProcessConfig a RgbContext para reutilizar funciones existentes.
 *
 * Esta función actúa como adaptador entre la nueva interfaz (ProcessConfig)
 * y la implementación interna (RgbContext).
 */
static void config_to_rgb_context(const ProcessConfig *cfg, RgbContext *ctx) {
    rgb_context_init(ctx);

    // Copiar opciones básicas
    ctx->opts.input_file = cfg->input_file;
    ctx->opts.mode = cfg->strategy ? cfg->strategy : "daynite";
    ctx->opts.gamma = cfg->gamma;
    ctx->opts.scale = cfg->scale;

    // Opciones booleanas
    ctx->opts.do_reprojection = cfg->do_reprojection;
    ctx->opts.apply_histogram = cfg->apply_histogram;
    ctx->opts.force_geotiff = cfg->force_geotiff;
    ctx->opts.apply_rayleigh = cfg->apply_rayleigh;
    ctx->opts.rayleigh_analytic = cfg->rayleigh_analytic;
    ctx->opts.use_citylights = cfg->use_citylights;
    ctx->opts.use_alpha = cfg->use_alpha;
    ctx->opts.use_full_res = cfg->use_full_res;

    // CLAHE
    ctx->opts.apply_clahe = cfg->apply_clahe;
    if (cfg->apply_clahe) {
        ctx->opts.clahe_tiles_x = cfg->clahe_tiles_x;
        ctx->opts.clahe_tiles_y = cfg->clahe_tiles_y;
        ctx->opts.clahe_clip_limit = cfg->clahe_clip_limit;
    }

    // Clip
    ctx->opts.has_clip = cfg->has_clip;
    if (cfg->has_clip) {
        for (int i = 0; i < 4; i++) {
            ctx->opts.clip_coords[i] = cfg->clip_coords[i];
        }
    }

    // Custom mode
    // Cast para silenciar el warning -Wdiscarded-qualifiers. La solución ideal
    // es cambiar el tipo de 'expr' y 'minmax' en RgbOptions a 'const char*'.
    ctx->opts.expr = (char *)cfg->custom_expr;
    ctx->opts.minmax = (char *)cfg->custom_minmax;

    // Output filename
    if (cfg->output_path_override) {
        ctx->opts.output_filename = (char *)cfg->output_path_override;
        ctx->opts.output_generated = false;
    }

    // Detectar producto L2
    const char *basename_input = strrchr(cfg->input_file, '/');
    basename_input = basename_input ? basename_input + 1 : cfg->input_file;
    ctx->opts.is_l2_product = (strstr(basename_input, "CMIP") != NULL);
}

int run_rgb(const ProcessConfig *cfg, MetadataContext *meta) {
    if (!cfg || !meta) {
        LOG_ERROR("run_rgb: parámetros NULL");
        return 1;
    }

    LOG_INFO("Procesando RGB: %s", cfg->input_file);

    RgbContext ctx;
    config_to_rgb_context(cfg, &ctx);
    int status = 1;
    char **custom_channels = NULL;

    // Registrar metadatos básicos
    metadata_add(meta, "command", "rgb");
    metadata_add(meta, "mode", ctx.opts.mode ? ctx.opts.mode : "unknown");
    metadata_add(meta, "gamma", ctx.opts.gamma);
    metadata_add(meta, "apply_clahe", ctx.opts.apply_clahe);
    metadata_add(meta, "apply_rayleigh", ctx.opts.apply_rayleigh);
    metadata_add(meta, "apply_histogram", ctx.opts.apply_histogram);
    metadata_add(meta, "do_reprojection", ctx.opts.do_reprojection);

    if (ctx.opts.apply_clahe) {
        metadata_add(meta, "clahe_limit", ctx.opts.clahe_clip_limit);
    }

    // Obtener estrategia
    const RgbStrategy *strategy = get_strategy_for_mode(ctx.opts.mode);
    if (!strategy) {
        LOG_ERROR("Modo '%s' no reconocido.", ctx.opts.mode);
        goto cleanup;
    }
    LOG_INFO("Modo seleccionado: %s - %s", strategy->mode_name, strategy->description);

    const char **req_channels = NULL;
    if (strcmp(ctx.opts.mode, "custom") == 0) {
        if (!ctx.opts.expr) {
            LOG_ERROR("El modo 'custom' requiere especificar --expr");
            goto cleanup;
        }
        int count = get_unique_channels_rgb(ctx.opts.expr, &custom_channels);
        if (count == 0 || !custom_channels) {
            LOG_ERROR("No se detectaron bandas válidas en: %s", ctx.opts.expr);
            goto cleanup;
        }
        LOG_INFO("Modo Custom: Se requieren %d bandas", count);
        req_channels = (const char **)custom_channels;
    } else {
        req_channels = (const char **)strategy->req_channels;
    }

    // Cargar canales
    if (!load_channels(&ctx, req_channels)) {
        LOG_ERROR("%s", ctx.error_msg);
        goto cleanup;
    }

    // Extraer metadatos del canal de referencia (satélite, timestamp, geometría nativa)
    metadata_from_nc(meta, &ctx.channels[ctx.ref_channel_idx]);

    // Procesar geoespacial
    if (!process_geospatial(&ctx, strategy)) {
        LOG_ERROR("%s", ctx.error_msg);
        goto cleanup;
    }

    // Composición
    LOG_INFO("Generando compuesto '%s'...", strategy->mode_name);
    if (!strategy->composer_func(&ctx)) {
        LOG_ERROR("Falla al generar compuesto RGB");
        goto cleanup;
    }

    // Preprocesar DataF
    if (ctx.comp_r.data_in && ctx.comp_g.data_in && ctx.comp_b.data_in) {
        if (ctx.opts.gamma > 0.0f && fabsf(ctx.opts.gamma - 1.0f) > 1e-6) {
            LOG_INFO("Aplicando Gamma %.2f", ctx.opts.gamma);
            dataf_apply_gamma(&ctx.comp_r, ctx.opts.gamma);
            dataf_apply_gamma(&ctx.comp_g, ctx.opts.gamma);
            dataf_apply_gamma(&ctx.comp_b, ctx.opts.gamma);
            ctx.opts.gamma = 1.0f;
        }

        // Renderizar a imagen
        ctx.final_image =
            create_multiband_rgb(&ctx.comp_r, &ctx.comp_g, &ctx.comp_b, ctx.min_r, ctx.max_r,
                                 ctx.min_g, ctx.max_g, ctx.min_b, ctx.max_b);
    }

    if (ctx.final_image.data == NULL) {
        LOG_ERROR("Falla al generar imagen RGB");
        goto cleanup;
    }

    // Reproyección
    if (ctx.opts.do_reprojection) {
        if (!ctx.has_navigation) {
            LOG_ERROR("Navegación requerida para reproyección");
            goto cleanup;
        }

        LOG_INFO("Iniciando reproyección...");
        ImageData reprojected =
            reproject_image_to_geographics(&ctx.final_image, &ctx.nav_lat, &ctx.nav_lon,
                                           ctx.channels[ctx.ref_channel_idx].native_resolution_km,
                                           ctx.opts.has_clip ? ctx.opts.clip_coords : NULL);

        if (reprojected.data == NULL) {
            LOG_ERROR("Falla durante reproyección");
            goto cleanup;
        }

        image_destroy(&ctx.final_image);
        ctx.final_image = reprojected;

        // Actualizar límites
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

    // Post-procesamiento
    if (!apply_postprocessing(&ctx)) {
        LOG_ERROR("Falla en post-procesamiento");
        goto cleanup;
    }

    // Escritura
    if (!write_output(&ctx)) {
        LOG_ERROR("Falla al guardar imagen");
        goto cleanup;
    }

    // Actualizar metadatos finales
    metadata_add(meta, "output_file", ctx.opts.output_filename);
    metadata_add(meta, "output_width", (int)ctx.final_image.width);
    metadata_add(meta, "output_height", (int)ctx.final_image.height);

    LOG_INFO("✅ Imagen RGB guardada: %s", ctx.opts.output_filename);
    status = 0;

cleanup:
    rgb_context_destroy(&ctx);
    if (custom_channels) {
        for (int i = 0; custom_channels[i] != NULL; i++) {
            free(custom_channels[i]);
        }
        free(custom_channels);
    }
    return status;
}
