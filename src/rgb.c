/* RGB and day/night composite generation for ABI multi-band imagery.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "clip_loader.h"
#include "config.h"
#include "datanc.h"
#include "daynight_mask.h"
#include "image.h"
#include "logger.h"
#include "metadata.h"
#include "nocturnal_pseudocolor.h"
#include "parse_expr.h"
#include "processing.h"
#include "rayleigh.h"
#include "reader_nc.h"
#include "reader_webp.h"
#include "reprojection.h"
#include "rgb.h"
#include "truecolor.h"
#include "writer_geotiff.h"
#include "writer_png.h"

void rgb_context_init(RgbContext *ctx) {
    memset(ctx, 0, sizeof(RgbContext));
    // Inicializar defaults de opciones
    ctx->opts.gamma[0] = ctx->opts.gamma[1] = ctx->opts.gamma[2] = 1.0f;
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

    for (int i = 1; i <= 16; i++) {
        datanc_destroy(&ctx->channels[i]);
    }

    dataf_destroy(&ctx->nav_lat);
    dataf_destroy(&ctx->nav_lon);

    dataf_destroy(&ctx->comp_r);
    dataf_destroy(&ctx->comp_g);
    dataf_destroy(&ctx->comp_b);

    image_destroy(&ctx->final_image);
    image_destroy(&ctx->alpha_mask);

    if (ctx->opts.output_generated && ctx->opts.output_filename) {
        free(ctx->opts.output_filename);
    }
}


// --- PHASE 2: COMPOSERS (STRATEGY PATTERN) ---

static bool compose_truecolor(RgbContext *ctx) {
    // 1. Setup y Copia
    DataF *ch_blue = &ctx->channels[1].fdata; // C01
    DataF *ch_red = &ctx->channels[2].fdata;  // C02
    DataF *ch_nir = &ctx->channels[3].fdata;  // C03

    ctx->comp_b = dataf_copy(ch_blue);
    ctx->comp_r = dataf_copy(ch_red);
    if (!ctx->comp_b.data_in || !ctx->comp_r.data_in)
        return false;

    // Rayleigh correction.
    if (ctx->opts.apply_rayleigh || ctx->opts.rayleigh_analytic) {
        // Locate the C01 file for navigation.
        const char *nav_file = NULL;
        for (int i = 0; i < ctx->channel_set->count; i++) {
            if (strcmp(ctx->channel_set->channels[i].name, "C01") == 0) {
                nav_file = ctx->channel_set->channels[i].filename;
                break;
            }
        }
        RayleighNav nav = {0};
        // Reuse pre-computed lat/lon to avoid a costly compute_navigation_nc call.
        bool nav_ok;
        if (ctx->has_navigation && ctx->nav_lat.data_in && ctx->nav_lon.data_in) {
            nav_ok = rayleigh_load_navigation_from_latlon(nav_file, &ctx->nav_lat, &ctx->nav_lon,
                                                         &nav, ctx->comp_b.width, ctx->comp_b.height);
        } else {
            nav_ok = rayleigh_load_navigation(nav_file, &nav, ctx->comp_b.width, ctx->comp_b.height);
        }
        if (nav_ok) {
            apply_solar_zenith_correction(&ctx->comp_b, &nav.sza);
            apply_solar_zenith_correction(&ctx->comp_r, &nav.sza);
            apply_solar_zenith_correction(ch_nir, &nav.sza);
            if (ctx->opts.rayleigh_analytic) {
                LOG_INFO("Aplicando Rayleigh analítico...");
                analytic_rayleigh_correction(&ctx->comp_b, &nav, 0.47);
                analytic_rayleigh_correction(&ctx->comp_r, &nav, 0.64);
            } else {
                LOG_INFO("Aplicando Rayleigh LUT...");
                luts_rayleigh_correction(&ctx->comp_b, &nav, 1, &ctx->comp_r);
                luts_rayleigh_correction(&ctx->comp_r, &nav, 2, NULL);
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
    // Green formula already uses geo2grid-matched CIMSS coefficients; no extra boost needed.

    // 3b. Ratio Sharpening
    if (ctx->opts.use_sharpen) {
        LOG_DEBUG("Aplicando ratio sharpening...");
        DataF ratio_map = dataf_ratio_sharpen_map(&ctx->comp_r);
        if (ratio_map.data_in) {
            DataF new_g = dataf_op_dataf(&ctx->comp_g, &ratio_map, OP_MUL);
            dataf_destroy(&ctx->comp_g);
            ctx->comp_g = new_g;
            DataF new_b = dataf_op_dataf(&ctx->comp_b, &ratio_map, OP_MUL);
            dataf_destroy(&ctx->comp_b);
            ctx->comp_b = new_b;
            dataf_destroy(&ratio_map);
        }
    }

    if (ctx->opts.use_piecewise_stretch) {
        LOG_DEBUG("Aplicando piecewise stretch...");
        apply_piecewise_stretch(&ctx->comp_r);
        apply_piecewise_stretch(&ctx->comp_g);
        apply_piecewise_stretch(&ctx->comp_b);
    }

    // With piecewise stretch, output is in [0, 1.0];
    // without it, reflectance can exceed 1.0 but is clamped in the 8-bit conversion.
    float range_max = ctx->opts.use_piecewise_stretch ? 1.0f : 1.1f;
    ctx->min_r = 0.0f;
    ctx->max_r = range_max;
    ctx->min_g = 0.0f;
    ctx->max_g = range_max;
    ctx->min_b = 0.0f;
    ctx->max_b = range_max;

    return true;
}

static bool compose_night(RgbContext *ctx) {
    // Cargar imagen de fondo (luces de ciudad) si se solicita
    ImageData fondo_img = {0};
    const ImageData *fondo_ptr = NULL;
    if (ctx->opts.use_citylights) {
        int width = ctx->channels[ctx->ref_channel_idx].fdata.width;
        const char *bg_path = NULL;

        if (width == 2500) {
            bg_path = "/usr/local/share/lanot/images/land_lights_2016_conus.webp";
        } else if (width == 5424) {
            bg_path = "/usr/local/share/lanot/images/land_lights_2016_fd.webp";
        } else if (width == 8987) {
            bg_path = "/usr/local/share/lanot/images/land_lights_2016_lalo.webp";
        } else {
            LOG_WARN("Resolución (%d) no coincide con fondos disponibles. Se omiten luces.", width);
        }

        if (bg_path) {
            LOG_INFO("Cargando imagen de fondo: %s", bg_path);
            fondo_img = reader_load_webp(bg_path);
            if (fondo_img.data != NULL) {
                fondo_ptr = &fondo_img;
            } else {
                LOG_WARN("No se pudo cargar la imagen de fondo de luces de ciudad.");
            }
        }
    } else {
        LOG_INFO("Luces de ciudad desactivadas. Use -l o --citylights para activarlas.");
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

static bool compose_severestorm(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[8].fdata, &ctx->channels[10].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[7].fdata, &ctx->channels[13].fdata, OP_SUB);
    ctx->comp_b = dataf_op_dataf(&ctx->channels[5].fdata, &ctx->channels[2].fdata, OP_SUB);
    ctx->min_r = -35.0f;
    ctx->max_r = 5.0f;
    ctx->min_g = -5.0f;
    ctx->max_g = 60.0f;
    ctx->min_b = -0.75f;
    ctx->max_b = 0.25f;
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
    ctx->opts.use_piecewise_stretch = true;
    if (!compose_truecolor(ctx)) {
        return false;
    }
    // Forzamos el uso de citylights para la nocturna.
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
    LOG_DEBUG("Rangos custom RGB: %s: %f,%f  %f,%f %f,%f", ctx->opts.minmax, ranges[0][0],
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
    {"severestorm", {"C02", "C05", "C07", "C08","C10", "C13", NULL}, compose_severestorm, "Severe Convection RGB", false},
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
    find_scan_mode_from_name(basename_input, ctx->channel_set->scan_mode,
                             sizeof(ctx->channel_set->scan_mode));
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
                // Select highest resolution (smallest km value) for --full-res.
                if (ctx->ref_channel_idx == 0 ||
                    ctx->channels[cn].native_resolution_km <
                        ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
                    ctx->ref_channel_idx = cn;
                }
            } else {
                // Default: select lowest resolution (largest km value).
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
        int cn = atoi(ctx->channel_set->channels[i].name + 1);
        if (cn == ctx->ref_channel_idx || ctx->channels[cn].fdata.data_in == NULL)
            continue;

        float res = ctx->channels[cn].native_resolution_km;
        float factor_f = res / ref_res;

        if (fabs(factor_f - 1.0f) > 0.01f) {
            int factor = (int)(factor_f + 0.5f);
            DataF resampled = {0};

            if (factor_f < 1.0f) { // this channel has finer resolution than reference -> downsample
                factor = (int)((1.0f / factor_f) + 0.5f);
                LOG_INFO("Downsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res,
                         factor);
                resampled = downsample_boxfilter(ctx->channels[cn].fdata, factor);
            } else { // this channel has coarser resolution than reference -> upsample
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
    // Compute navigation using the reference channel file (already at the target resolution)
    // to avoid computing at full resolution and then resampling.
    const char *ref_filename = NULL;
    char ref_name[8];
    snprintf(ref_name, sizeof(ref_name), "C%02d", ctx->ref_channel_idx);
    for (int i = 0; i < ctx->channel_set->count; i++) {
        if (strcmp(ctx->channel_set->channels[i].name, ref_name) == 0) {
            ref_filename = ctx->channel_set->channels[i].filename;
            break;
        }
    }
    if (!ref_filename)
        ref_filename = ctx->channel_set->channels[0].filename; // fallback
    if (compute_navigation_nc(ref_filename, &ctx->nav_lat, &ctx->nav_lon) == 0) {
        ctx->has_navigation = true;
    } else {
        LOG_WARN("No se pudieron cargar los datos de navegación.");
        ctx->has_navigation = false;
    }

    // Check if strategy requires navigation.
    if (strategy->needs_navigation && !ctx->has_navigation) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "El modo '%s' requiere datos de navegación, pero no se pudieron "
                 "cargar.",
                 strategy->mode_name);
        return false;
    }

    // Resample navigation if it differs from the reference channel (rare in practice).
    if (ctx->has_navigation && ctx->ref_channel_idx > 0) {
        size_t nav_width = ctx->nav_lat.width;
        size_t ref_width = ctx->channels[ctx->ref_channel_idx].fdata.width;

        if (nav_width != ref_width) {
            if (nav_width > ref_width) {
                int factor = nav_width / ref_width;
                LOG_DEBUG("Remuestreando navegación (downsample factor %d)",
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
                int factor = ref_width / nav_width;
                LOG_DEBUG("Remuestreando navegación (upsample factor %d)",
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

static bool apply_enhancements(RgbContext *ctx) {
    // daynite is a special composite mode: blend day (visible) and night (IR) images.
    if (strcmp(ctx->opts.mode, "daynite") == 0) {
        // Navigation is already resampled to reference resolution in process_geospatial.
        DataF *nav_lat_ptr = &ctx->nav_lat;
        DataF *nav_lon_ptr = &ctx->nav_lon;

        float day_pct = 0.0f;
        ImageData mask = create_daynight_mask(ctx->channels[13], *nav_lat_ptr, *nav_lon_ptr,
                                              &day_pct, ctx->opts.cloud_temp);
        float night_pct = 100.0f - day_pct;

        // Blend if nighttime fraction exceeds 0.1%; for fully nocturnal scenes night_pct=100.
        if (night_pct > 0.1f && mask.data) {
            LOG_INFO("Blending day/night images (night: %.2f%%)", night_pct);
            ctx->final_image = blend_images(ctx->alpha_mask, ctx->final_image, mask);
        } else {
            LOG_INFO("Scene is mostly daytime (%.2f%%), using only visible composite.", day_pct);
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

    if (ctx->opts.use_alpha) {
        ctx->alpha_mask =
            image_create_alpha_mask_from_dataf(&ctx->channels[ctx->ref_channel_idx].fdata);
    }

    if (ctx->opts.use_alpha && ctx->alpha_mask.data) {
        ImageData with_alpha = image_add_alpha_channel(&ctx->final_image, &ctx->alpha_mask);
        if (with_alpha.data) {
            image_destroy(&ctx->final_image);
            ctx->final_image = with_alpha;
        }
        image_destroy(&ctx->alpha_mask);
        memset(&ctx->alpha_mask, 0, sizeof(ImageData));
    }
    
    return true;
}

static bool apply_scaling(RgbContext *ctx) {
    if (ctx->opts.scale != 1) {
        ImageData scaled_img = {0};
        if (ctx->opts.scale < 0) {
            LOG_INFO("Reduciendo imagen por factor %d", -ctx->opts.scale);
            scaled_img = image_downsample_boxfilter(&ctx->final_image, -ctx->opts.scale);
        } else { // scale > 1
            LOG_INFO("Ampliando imagen por factor %d", ctx->opts.scale);
            scaled_img = image_upsample_bilinear(&ctx->final_image, ctx->opts.scale);
        }
        
        if (scaled_img.data) {
            image_destroy(&ctx->final_image);
            ctx->final_image = scaled_img;
        } else {
            LOG_ERROR("Falla al escalar imagen");
            return false;
        }
    }
    return true;
}

static bool write_output(RgbContext *ctx, const char *product_label) {
    bool is_geotiff = ctx->opts.force_geotiff ||
                      (ctx->opts.output_filename && (strstr(ctx->opts.output_filename, ".tif") ||
                                                     strstr(ctx->opts.output_filename, ".tiff")));

    if (is_geotiff) {
        LOG_DEBUG("Formato de salida: GeoTIFF");
        DataNC meta_out = ctx->channels[ctx->ref_channel_idx];  // Preserva sat_id, sector_id, band_id, timestamp, etc.
        if (ctx->opts.do_reprojection) {
            meta_out.proj_code = PROJ_LATLON;
            meta_out.proj_info.valid = false;  // No aplica para lat/lon
            meta_out.geotransform[0] = ctx->final_lon_min;
            meta_out.geotransform[1] = (ctx->final_lon_max - ctx->final_lon_min) / (double)ctx->final_image.width;
            meta_out.geotransform[2] = 0.0;
            meta_out.geotransform[3] = ctx->final_lat_max;
            meta_out.geotransform[4] = 0.0;
            meta_out.geotransform[5] = (ctx->final_lat_min - ctx->final_lat_max) / (double)ctx->final_image.height;
        } else {
            // Metadatos nativos (geoestacionarios)
            meta_out = ctx->channels[ctx->ref_channel_idx];
            
            // 1. Aplicar offset de recorte al origen (en radianes originales)
            meta_out.geotransform[0] += ctx->crop_x_offset * meta_out.geotransform[1];
            meta_out.geotransform[3] += ctx->crop_y_offset * meta_out.geotransform[5];
            
            // Adjust pixel scale in geotransform if the image was scaled.
            if (ctx->opts.scale != 1) {
                double scale_factor = (ctx->opts.scale < 0) ? -ctx->opts.scale : ctx->opts.scale;
                if (ctx->opts.scale > 1) {
                    meta_out.geotransform[1] /= scale_factor;
                    meta_out.geotransform[5] /= scale_factor;
                } else {
                    meta_out.geotransform[1] *= scale_factor;
                    meta_out.geotransform[5] *= scale_factor;
                }
            }
        }
        // Pasamos 0,0 como offset porque ya lo integramos en meta_out.geotransform
        write_geotiff_rgb(ctx->opts.output_filename, &ctx->final_image, &meta_out,
                          0, 0, product_label);
    } else {
        writer_save_png(ctx->opts.output_filename, &ctx->final_image);
    }
    return true;
}

// =============================================================================
// UNIFIED INTERFACE (dependency injection via ProcessConfig)
// =============================================================================

// Adapts ProcessConfig to RgbContext, bridging the stable public API and the
// internal RgbContext implementation.
static void config_to_rgb_context(const ProcessConfig *cfg, RgbContext *ctx) {
    rgb_context_init(ctx);

    ctx->opts.input_file = cfg->input_file;
    // Normalizar 'default' a 'daynite' para comparaciones de string posteriores
    if (cfg->strategy && strcmp(cfg->strategy, "default") == 0) {
        ctx->opts.mode = "daynite";
    } else {
        ctx->opts.mode = cfg->strategy ? cfg->strategy : "daynite";
    }
    ctx->opts.gamma[0] = cfg->gamma[0];
    ctx->opts.gamma[1] = cfg->gamma[1];
    ctx->opts.gamma[2] = cfg->gamma[2];
    ctx->opts.scale = cfg->scale;

    // Opciones booleanas
    ctx->opts.do_reprojection = cfg->do_reprojection;
    ctx->opts.save_both = cfg->save_both;
    ctx->opts.apply_histogram = cfg->apply_histogram;
    ctx->opts.force_geotiff = cfg->force_geotiff;
    ctx->opts.apply_rayleigh = cfg->apply_rayleigh;
    ctx->opts.rayleigh_analytic = cfg->rayleigh_analytic;
    ctx->opts.use_piecewise_stretch = cfg->use_piecewise_stretch;
    ctx->opts.use_sharpen = cfg->use_sharpen;
    ctx->opts.use_citylights = cfg->use_citylights;
    ctx->opts.use_alpha = cfg->use_alpha;
    ctx->opts.use_full_res = cfg->use_full_res;
    ctx->opts.cloud_temp = cfg->cloud_temp;

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

    // Cast to silence -Wdiscarded-qualifiers; the clean fix is to change
    // RgbOptions.expr/minmax to 'const char*'.
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

    // Use the short product name (-N flag) if provided; otherwise fall back to mode string.
    const char *mode_label = (cfg->product_short && cfg->product_short[0])
                             ? cfg->product_short
                             : (ctx.opts.mode ? ctx.opts.mode : "unknown");
    metadata_add(meta, "mode", mode_label);
    if (fabsf(ctx.opts.gamma[0] - ctx.opts.gamma[1]) < 1e-6f &&
        fabsf(ctx.opts.gamma[0] - ctx.opts.gamma[2]) < 1e-6f) {
        metadata_add(meta, "gamma", ctx.opts.gamma[0]);
    } else {
        char gamma_str[48];
        snprintf(gamma_str, sizeof(gamma_str), "%.4g;%.4g;%.4g",
                 ctx.opts.gamma[0], ctx.opts.gamma[1], ctx.opts.gamma[2]);
        metadata_add(meta, "gamma", (const char*)gamma_str);
    }
    metadata_add(meta, "apply_clahe", ctx.opts.apply_clahe);
    metadata_add(meta, "apply_rayleigh", ctx.opts.apply_rayleigh);
    metadata_add(meta, "apply_histogram", ctx.opts.apply_histogram);
    metadata_add(meta, "geographics", (bool)(ctx.opts.do_reprojection && !ctx.opts.save_both));
    if (ctx.opts.has_clip)
        metadata_set_clip(meta, true);

    if (ctx.opts.apply_clahe) {
        metadata_add(meta, "clahe_limit", ctx.opts.clahe_clip_limit);
    }

    // Obtener estrategia
    const RgbStrategy *strategy = get_strategy_for_mode(ctx.opts.mode);
    if (!strategy) {
        LOG_ERROR("Modo '%s' no reconocido.", ctx.opts.mode);

        char available[512] = {0};
        for (int i = 0; STRATEGIES[i].mode_name != NULL; i++) {
            if (i > 0) strcat(available, ", ");
            strcat(available, STRATEGIES[i].mode_name);
        }
        LOG_INFO("Modos disponibles: %s", available);
        goto cleanup;
    }
    LOG_INFO("Modo seleccionado: %s - %s", strategy->mode_name, strategy->description);

    // Determinar nombre descriptivo del producto
    const char *product = cfg->product_long ? cfg->product_long : strategy->description;
    metadata_set_product(meta, product);

    // Rayleigh correction is not meaningful for night mode (thermal IR only).
    if (strcmp(ctx.opts.mode, "night") == 0) {
        if (ctx.opts.apply_rayleigh || ctx.opts.rayleigh_analytic) {
            LOG_WARN("La corrección Rayleigh se ignora en modo 'night' (solo afecta canales visibles).");
        }
        if (ctx.opts.use_piecewise_stretch) {
            LOG_WARN("El estiramiento de contraste (stretch) se ignora en modo 'night'.");
        }
    }

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

    // Extract satellite/band/timestamp/geometry metadata from reference channel.
    metadata_from_nc(meta, &ctx.channels[ctx.ref_channel_idx]);

    // Procesar geoespacial
    if (!process_geospatial(&ctx, strategy)) {
        LOG_ERROR("%s", ctx.error_msg);
        goto cleanup;
    }

    // RGB composite.
    LOG_INFO("Generating '%s' composite...", strategy->mode_name);
    if (!strategy->composer_func(&ctx)) {
        LOG_ERROR("Falla al generar compuesto RGB");
        goto cleanup;
    }

    // Preprocesar DataF
    if (ctx.comp_r.data_in && ctx.comp_g.data_in && ctx.comp_b.data_in) {
        bool any_gamma = fabsf(ctx.opts.gamma[0] - 1.0f) > 1e-6f ||
                         fabsf(ctx.opts.gamma[1] - 1.0f) > 1e-6f ||
                         fabsf(ctx.opts.gamma[2] - 1.0f) > 1e-6f;
        if (any_gamma) {
            LOG_INFO("Aplicando Gamma R=%.2f G=%.2f B=%.2f",
                     ctx.opts.gamma[0], ctx.opts.gamma[1], ctx.opts.gamma[2]);
            // Solo actualizar el rango a [0,1] en canales donde gamma != 1.0;
            // de lo contrario dataf_apply_gamma no modifica los datos y el rango
            // del --minmax (ya en ctx.min_*/max_*) debe conservarse para el render.
            if (fabsf(ctx.opts.gamma[0] - 1.0f) > 1e-6f) {
                dataf_apply_gamma(&ctx.comp_r, ctx.opts.gamma[0], ctx.min_r, ctx.max_r);
                ctx.min_r = 0.0f;  ctx.max_r = 1.0f;
            }
            if (fabsf(ctx.opts.gamma[1] - 1.0f) > 1e-6f) {
                dataf_apply_gamma(&ctx.comp_g, ctx.opts.gamma[1], ctx.min_g, ctx.max_g);
                ctx.min_g = 0.0f;  ctx.max_g = 1.0f;
            }
            if (fabsf(ctx.opts.gamma[2] - 1.0f) > 1e-6f) {
                dataf_apply_gamma(&ctx.comp_b, ctx.opts.gamma[2], ctx.min_b, ctx.max_b);
                ctx.min_b = 0.0f;  ctx.max_b = 1.0f;
            }
            ctx.opts.gamma[0] = ctx.opts.gamma[1] = ctx.opts.gamma[2] = 1.0f;
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

    // Post-processing (blending, CLAHE, alpha) — before reprojection.
    if (!apply_enhancements(&ctx)) {
        LOG_ERROR("Falla en post-procesamiento (enhancements)");
        goto cleanup;
    }

    // -B: escalar y guardar fixed-grid antes de reproyectar
    if (ctx.opts.save_both) {
        if (!apply_scaling(&ctx)) {
            LOG_ERROR("Falla en escalado (fixed-grid)");
            goto cleanup;
        }
        if (ctx.opts.output_filename == NULL) {
            const char *ext_fg = ctx.opts.force_geotiff ? ".tif" : ".png";
            ctx.opts.output_filename = metadata_build_filename(meta, ext_fg);
            ctx.opts.output_generated = true;
            if (ctx.opts.output_filename == NULL) {
                LOG_ERROR("Falla al generar nombre de archivo fixed-grid");
                goto cleanup;
            }
        }
        LOG_INFO("Guardando fixed-grid: %s", ctx.opts.output_filename);
        // Temporarily disable reprojection flag so write_output uses the native projection.
        ctx.opts.do_reprojection = false;
        if (!write_output(&ctx, product)) {
            LOG_ERROR("Falla al guardar fixed-grid");
            goto cleanup;
        }
        ctx.opts.do_reprojection = true;
        // Append _geo suffix to the filename for the reprojected output.
        char *geo_filename = insert_geo_suffix(ctx.opts.output_filename);
        if (ctx.opts.output_generated) {
            free(ctx.opts.output_filename);
        }
        ctx.opts.output_filename = geo_filename;
        ctx.opts.output_generated = true;
        if (ctx.opts.output_filename == NULL) {
            LOG_ERROR("Falla al generar nombre de archivo reproyectado");
            goto cleanup;
        }
        LOG_INFO("Guardando reproyectado: %s", ctx.opts.output_filename);
    }

    // Reprojection.
    if (ctx.opts.do_reprojection) {
        if (!ctx.has_navigation) {
            LOG_ERROR("Navegación requerida para reproyección");
            goto cleanup;
        }

        LOG_INFO("Iniciando reproyección...");
        ImageData reprojected =
            reproject_image_analytical(&ctx.final_image,
                                       &ctx.channels[ctx.ref_channel_idx],
                                       ctx.nav_lat.fmin, ctx.nav_lat.fmax,
                                       ctx.nav_lon.fmin, ctx.nav_lon.fmax,
                                       ctx.channels[ctx.ref_channel_idx].native_resolution_km,
                                       ctx.opts.has_clip ? ctx.opts.clip_coords : NULL);

        if (reprojected.data == NULL) {
            LOG_ERROR("Falla durante reproyección");
            goto cleanup;
        }

        image_destroy(&ctx.final_image);
        ctx.final_image = reprojected;

        // Update final bounding box from reprojected extent.
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
    } else {
        // No reprojection: apply clip in native fixed-grid coordinates if requested.
        if (ctx.opts.has_clip && ctx.has_navigation) {
            int ix, iy, iw, ih;
            reprojection_find_bounding_box(&ctx.nav_lat, &ctx.nav_lon, 
                ctx.opts.clip_coords[0], ctx.opts.clip_coords[1], 
                ctx.opts.clip_coords[2], ctx.opts.clip_coords[3], 
                &ix, &iy, &iw, &ih);
            
            // Aplicar el recorte a la imagen generada
            ImageData cropped = image_crop(&ctx.final_image, ix, iy, iw, ih);
            image_destroy(&ctx.final_image);
            ctx.final_image = cropped;
            
            ctx.crop_x_offset = (unsigned)ix;
            ctx.crop_y_offset = (unsigned)iy;
        } else if (ctx.has_navigation) {
            ctx.final_lon_min = ctx.nav_lon.fmin;
            ctx.final_lon_max = ctx.nav_lon.fmax;
            ctx.final_lat_min = ctx.nav_lat.fmin;
            ctx.final_lat_max = ctx.nav_lat.fmax;
        }
    }

    // Write geometry metadata for JSON sidecar.
    if (ctx.has_navigation || ctx.opts.has_clip) {
        if (ctx.opts.do_reprojection) {
            metadata_set_geometry(meta, ctx.final_lon_min, ctx.final_lat_min, ctx.final_lon_max, ctx.final_lat_max);
            metadata_set_projection(meta, "EPSG:4326");
        } else {
            // Compute bounds in metres for geostationary projection metadata.
            DataNC *ref = &ctx.channels[ctx.ref_channel_idx];
            double *gt = ref->geotransform;
            double h = (ref->proj_info.valid) ? ref->proj_info.sat_height : 35786023.0;

            if (gt[1] != 0.0) {
                double x_min = (gt[0] + ctx.crop_x_offset * gt[1]) * h;
                double y_top = (gt[3] + ctx.crop_y_offset * gt[5]) * h;
                double x_max = x_min + (ctx.final_image.width * gt[1] * h);
                double y_bot = y_top + (ctx.final_image.height * gt[5] * h);
                
                double y_min = (y_bot < y_top) ? y_bot : y_top;
                double y_max = (y_bot > y_top) ? y_bot : y_top;
                metadata_set_geometry(meta, (float)x_min, (float)y_min, (float)x_max, (float)y_max);
            }

            const char* sat_crs = "geostationary";
            int sid = ctx.channels[ctx.ref_channel_idx].sat_id;
            if (sid == SAT_GOES16) sat_crs = "goes16";
            else if (sid == SAT_GOES17) sat_crs = "goes17";
            else if (sid == SAT_GOES18) sat_crs = "goes18";
            else if (sid == SAT_GOES19) sat_crs = "goes19";
            metadata_set_projection(meta, sat_crs);
        }
    }

    // Final scaling — after reprojection (for save_both, already applied before reprojection).
    if (!ctx.opts.save_both && !apply_scaling(&ctx)) {
        LOG_ERROR("Falla en escalado final");
        goto cleanup;
    }

    // Generar nombre de salida si no fue especificado
    if (ctx.opts.output_filename == NULL) {
        const char *ext = ctx.opts.force_geotiff ? ".tif" : ".png";
        ctx.opts.output_filename = metadata_build_filename(meta, ext);
        ctx.opts.output_generated = true;
        
        if (ctx.opts.output_filename == NULL) {
            LOG_ERROR("Falla al generar nombre de archivo de salida");
            goto cleanup;
        }
    }

    // Escritura
    if (!write_output(&ctx, product)) {
        LOG_ERROR("Falla al guardar imagen");
        goto cleanup;
    }

    // Actualizar metadatos finales
    metadata_add(meta, "output_file", ctx.opts.output_filename);
    metadata_add(meta, "output_width", (int)ctx.final_image.width);
    metadata_add(meta, "output_height", (int)ctx.final_image.height);

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
