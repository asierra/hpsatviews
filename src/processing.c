/*
 * Single-channel processing module (gray and pseudocolor)
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "processing.h"
#include "args.h"
#include "config.h"
#include "logger.h"
#include "metadata.h"
#include "reader_nc.h"
#include "reader_cpt.h"
#include "writer_png.h"
#include "writer_geotiff.h"
#include "filename_utils.h"
#include "reprojection.h"
#include "image.h"
#include "datanc.h"
#include "clip_loader.h"
#include "gray.h"
#include "parse_expr.h"
#include "channelset.h"
#include "palette.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <libgen.h>  // Para dirname()
#include <math.h>    // Para fabs()


bool strinstr(const char *main_str, const char *sub) {
    return (main_str && sub && strstr(main_str, sub));
}

// ============================================================================
// PIPELINE v2.0: Implementación usando ProcessConfig + MetadataContext
// ============================================================================

int run_processing(const ProcessConfig* cfg, MetadataContext* meta) {
    if (!cfg || !meta) {
        LOG_ERROR("run_processing: parámetros NULL");
        return 1;
    }
    
    LOG_INFO("Procesando: %s", cfg->input_file);
    
    int status = -1;
    bool is_pseudocolor = (cfg->palette_file != NULL);
    CPTData* cptdata = NULL;
    ColorArray *color_array = NULL;
    DataNC c01 = {0}, channels[17] = {0};
    DataF result_data = {0}, navla_full = {0}, navlo_full = {0};
    ImageData final_image = {0}, temp_image = {0};
    char *palette_name = NULL;
    char *generated_filename = NULL;
    bool nav_loaded = false;
    bool expr_mode = cfg->is_custom_mode;
    int num_required_channels = 0;
    char* required_channels[17] = {NULL};
    LinearCombo combo = {0};
    float minmax[2] = {0.0f, 255.0f};
    bool minmax_provided = false;
    
    // Registrar parámetros básicos en metadatos
    metadata_add(meta, "command", cfg->command);
    metadata_add(meta, "gamma", cfg->gamma);
    metadata_add(meta, "apply_clahe", cfg->apply_clahe);
    metadata_add(meta, "apply_histogram", cfg->apply_histogram);
    metadata_add(meta, "invert_values", cfg->invert_values);
    metadata_add(meta, "do_reprojection", cfg->do_reprojection);
    metadata_add(meta, "scale", cfg->scale);
    
    // --- Modo pseudocolor: cargar paleta ---
    if (is_pseudocolor) {
        metadata_add(meta, "palette", cfg->palette_file);
        cptdata = read_cpt_file(cfg->palette_file);
        if (cptdata) {
            color_array = cpt_to_color_array(cptdata);
            char *cpt_dup = strdup(cfg->palette_file);
            if (cpt_dup) {
                char *base = basename(cpt_dup);
                char *ext = strrchr(base, '.');
                if (ext) *ext = '\0';
                palette_name = strdup(base);
                free(cpt_dup);
            }
        } else {
            LOG_ERROR("No se pudo cargar el archivo de paleta: %s", cfg->palette_file);
            goto cleanup;
        }
    }
    
    // --- Modo expr (álgebra de bandas) ---
    if (expr_mode) {
        LOG_INFO("Modo álgebra de bandas: %s", cfg->custom_expr);
        metadata_add(meta, "expression", cfg->custom_expr);
        
        if (parse_expr_string(cfg->custom_expr, &combo) != 0) {
            LOG_ERROR("Error al parsear la expresión: %s", cfg->custom_expr);
            goto cleanup;
        }
        
        num_required_channels = extract_required_channels(&combo, required_channels);
        if (num_required_channels == 0) {
            LOG_ERROR("No se encontraron bandas válidas en la expresión.");
            goto cleanup;
        }
        
        if (cfg->custom_minmax) {
            if (sscanf(cfg->custom_minmax, "%f,%f", &minmax[0], &minmax[1]) == 2) {
                minmax_provided = true;
                LOG_INFO("Rango de salida especificado: [%.2f, %.2f]", minmax[0], minmax[1]);
            }
        }
        
        // Cargar múltiples canales
        ChannelSet* cset = channelset_create((const char**)required_channels, num_required_channels);
        if (!cset) {
            LOG_ERROR("No se pudo crear el ChannelSet.");
            goto cleanup;
        }
        
        char id_signature[256];
        char* input_dup = strdup(cfg->input_file);
        if (!input_dup) {
            LOG_ERROR("Error de memoria.");
            channelset_destroy(cset); goto cleanup;
        }
        const char* basename_input = basename(input_dup);
        
        if (find_id_from_name(basename_input, id_signature, sizeof(id_signature)) != 0) {
            LOG_ERROR("No se pudo extraer firma de identificación: %s", basename_input);
            free(input_dup); channelset_destroy(cset); goto cleanup;
        }
        strcpy(cset->id_signature, id_signature);
        free(input_dup);
        
        // Buscar archivos
        char* dir_dup = strdup(cfg->input_file);
        const char* dirnm = dirname(dir_dup);
        bool is_l2_product = strinstr(cfg->input_file, "CMIP");
        
        if (find_channel_filenames(dirnm, cset, is_l2_product) != 0) {
            LOG_ERROR("No se pudieron encontrar archivos en %s", dirnm);
            free(dir_dup); channelset_destroy(cset); goto cleanup;
        }
        free(dir_dup);
        
        // Cargar canales
        for (int i = 0; i < cset->count; i++) {
            int band_id = atoi(cset->channels[i].name + 1);
            if (band_id < 1 || band_id > 16) continue;
            
            LOG_INFO("Cargando canal %s", cset->channels[i].name);
            if (load_nc_sf(cset->channels[i].filename, &channels[band_id]) != 0) {
                LOG_ERROR("Fallo al cargar canal %s", cset->channels[i].name);
                channelset_destroy(cset); goto cleanup;
            }
        }
        
        // Identificar canal de referencia y resamplear
        int ref_channel_idx = 0;
        for (int i = 0; i < cset->count; i++) {
            int cn = atoi(cset->channels[i].name + 1);
            if (cfg->use_full_res) {
                if (ref_channel_idx == 0 || channels[cn].native_resolution_km < channels[ref_channel_idx].native_resolution_km) {
                    ref_channel_idx = cn;
                }
            } else {
                if (ref_channel_idx == 0 || channels[cn].native_resolution_km > channels[ref_channel_idx].native_resolution_km) {
                    ref_channel_idx = cn;
                }
            }
        }
        
        LOG_INFO("Canal de referencia: C%02d", ref_channel_idx);
        
        // Resamplear canales
        float ref_res = channels[ref_channel_idx].native_resolution_km;
        for (int i = 0; i < cset->count; i++) {
            int cn = atoi(cset->channels[i].name + 1);
            if (cn == ref_channel_idx) continue;
            
            float res = channels[cn].native_resolution_km;
            float factor_f = res / ref_res;
            
            if (fabs(factor_f - 1.0f) > 0.01f) {
                DataF resampled = {0};
                if (factor_f < 1.0f) {
                    int factor = (int)((1.0f / factor_f) + 0.5f);
                    resampled = downsample_boxfilter(channels[cn].fdata, factor);
                } else {
                    int factor = (int)(factor_f + 0.5f);
                    resampled = upsample_bilinear(channels[cn].fdata, factor);
                }
                if (resampled.data_in) {
                    dataf_destroy(&channels[cn].fdata);
                    channels[cn].fdata = resampled;
                }
            }
        }
        
        // Evaluar expresión
        result_data = evaluate_linear_combo(&combo, channels);
        if (!result_data.data_in) {
            LOG_ERROR("Fallo al evaluar expresión.");
            channelset_destroy(cset); goto cleanup;
        }
        
        if (!minmax_provided) {
            minmax[0] = result_data.fmin;
            minmax[1] = result_data.fmax;
        }
        
        c01 = channels[ref_channel_idx];
        c01.fdata = result_data;
        c01.is_float = true;
        result_data.data_in = NULL;
        channels[ref_channel_idx].fdata.data_in = NULL;
        
        channelset_destroy(cset);
        
    } else {
        // Modo normal: un solo canal
        if (load_nc_sf(cfg->input_file, &c01) != 0) {
            LOG_ERROR("No se pudo cargar: %s", cfg->input_file);
            goto cleanup;
        }
    }
    
    // Poblar metadatos desde DataNC
    metadata_from_nc(meta, &c01);
    
    // Generar nombre de archivo si no se especificó
    const char* outfn = cfg->output_path_override;
    
    if (!outfn) {
        // Determinar extensión
        const char* ext = (cfg->force_geotiff) ? ".tif" : ".png";
        generated_filename = metadata_build_filename(meta, ext);
        outfn = generated_filename;
        if (!outfn) {
            LOG_ERROR("No se pudo generar nombre de archivo");
            goto cleanup;
        }
    }
    
    LOG_INFO("Archivo de salida: %s", outfn);
    
    // Cargar navegación si es necesario
    bool is_geotiff = cfg->force_geotiff || (outfn && (strstr(outfn, ".tif") || strstr(outfn, ".tiff")));
    
    if (cfg->has_clip || is_geotiff || cfg->do_reprojection) {
        if (compute_navigation_nc(cfg->input_file, &navla_full, &navlo_full) == 0) {
            nav_loaded = true;
        } else {
            LOG_WARN("No se pudo cargar navegación");
            if (is_geotiff) {
                LOG_ERROR("Navegación requerida para GeoTIFF");
                goto cleanup;
            }
        }
    }
    
    // Aplicar gamma
    if (cfg->gamma != 1.0f && c01.is_float && c01.fdata.data_in) {
        LOG_INFO("Aplicando gamma %.2f", cfg->gamma);
        dataf_apply_gamma(&c01.fdata, cfg->gamma);
    }
    
    // Crear imagen
    if (expr_mode) {
        final_image = create_single_gray_range(c01.fdata, cfg->invert_values, cfg->use_alpha, minmax[0], minmax[1]);
    } else if (c01.is_float) {
        final_image = create_single_gray(c01.fdata, cfg->invert_values, cfg->use_alpha, is_pseudocolor ? cptdata : NULL);
    } else {
        final_image = create_single_grayb(c01.bdata, cfg->invert_values, cfg->use_alpha, is_pseudocolor ? cptdata : NULL);
    }
    
    if (!final_image.data) {
        LOG_ERROR("Fallo al crear imagen");
        goto cleanup;
    }
    
    // Reproyección o recorte
    float final_lon_min = 0, final_lon_max = 0, final_lat_min = 0, final_lat_max = 0;
    unsigned crop_x_start = 0, crop_y_start = 0;
    
    if (cfg->do_reprojection) {
        if (!nav_loaded) {
            LOG_ERROR("Navegación requerida para reproyección");
            goto cleanup;
        }
        
        temp_image = reproject_image_to_geographics(
            &final_image, &navla_full, &navlo_full, c01.native_resolution_km,
            cfg->has_clip ? cfg->clip_coords : NULL
        );
        image_destroy(&final_image);
        final_image = temp_image;
        
        if (cfg->has_clip) {
            final_lon_min = cfg->clip_coords[0]; final_lat_max = cfg->clip_coords[1];
            final_lon_max = cfg->clip_coords[2]; final_lat_min = cfg->clip_coords[3];
        } else {
            final_lon_min = navlo_full.fmin; final_lon_max = navlo_full.fmax;
            final_lat_min = navla_full.fmin; final_lat_max = navla_full.fmax;
        }
        
    } else if (cfg->has_clip && nav_loaded) {
        int ix, iy, iw, ih;
        reprojection_find_bounding_box(&navla_full, &navlo_full, 
            cfg->clip_coords[0], cfg->clip_coords[1], 
            cfg->clip_coords[2], cfg->clip_coords[3], 
            &ix, &iy, &iw, &ih);
        
        temp_image = image_crop(&final_image, ix, iy, iw, ih);
        image_destroy(&final_image);
        final_image = temp_image;
        crop_x_start = (unsigned)ix;
        crop_y_start = (unsigned)iy;
    }
    
    // Post-procesamiento (solo para gray)
    if (!is_pseudocolor) {
        if (cfg->apply_histogram) image_apply_histogram(final_image);
        if (cfg->apply_clahe) {
            image_apply_clahe(final_image, cfg->clahe_tiles_x, cfg->clahe_tiles_y, cfg->clahe_clip_limit);
        }
    }
    
    // Remuestreo
    if (cfg->scale < 0) {
        temp_image = image_downsample_boxfilter(&final_image, -cfg->scale);
        image_destroy(&final_image);
        final_image = temp_image;
    } else if (cfg->scale > 1) {
        temp_image = image_upsample_bilinear(&final_image, cfg->scale);
        image_destroy(&final_image);
        final_image = temp_image;
    }
    
    // Guardar imagen
    if (is_geotiff) {
        DataNC meta_out = {0};
        
        if (cfg->do_reprojection) {
            meta_out.proj_code = PROJ_LATLON;
            meta_out.geotransform[0] = final_lon_min;
            meta_out.geotransform[1] = (final_lon_max - final_lon_min) / (double)final_image.width;
            meta_out.geotransform[2] = 0.0;
            meta_out.geotransform[3] = final_lat_max;
            meta_out.geotransform[4] = 0.0;
            meta_out.geotransform[5] = (final_lat_min - final_lat_max) / (double)final_image.height;
            
            if (is_pseudocolor && color_array) {
                if (cfg->use_alpha) {
                    temp_image = image_expand_palette(&final_image, color_array);
                    write_geotiff_rgb(outfn, &temp_image, &meta_out, 0, 0);
                    image_destroy(&temp_image);
                } else {
                    write_geotiff_indexed(outfn, &final_image, color_array, &meta_out, 0, 0);
                }
            } else {
                write_geotiff_gray(outfn, &final_image, &meta_out, 0, 0);
            }
        } else {
            meta_out = c01;
            if (cfg->scale != 1) {
                double scale_factor = (cfg->scale < 0) ? -cfg->scale : cfg->scale;
                if (cfg->scale > 1) {
                    meta_out.geotransform[1] /= scale_factor;
                    meta_out.geotransform[5] /= scale_factor;
                } else {
                    meta_out.geotransform[1] *= scale_factor;
                    meta_out.geotransform[5] *= scale_factor;
                }
            }
            
            if (is_pseudocolor && color_array) {
                if (cfg->use_alpha) {
                    temp_image = image_expand_palette(&final_image, color_array);
                    write_geotiff_rgb(outfn, &temp_image, &meta_out, crop_x_start, crop_y_start);
                    image_destroy(&temp_image);
                } else {
                    write_geotiff_indexed(outfn, &final_image, color_array, &meta_out, crop_x_start, crop_y_start);
                }
            } else {
                write_geotiff_gray(outfn, &final_image, &meta_out, crop_x_start, crop_y_start);
            }
        }
    } else {
        if (is_pseudocolor && color_array) {
            writer_save_png_palette(outfn, &final_image, color_array);
        } else {
            writer_save_png(outfn, &final_image);
        }
    }
    
    LOG_INFO("✅ Imagen guardada: %s", outfn);
    metadata_add(meta, "output_file", outfn);
    metadata_add(meta, "output_width", (int)final_image.width);
    metadata_add(meta, "output_height", (int)final_image.height);
    
    status = 0;

cleanup:
    if (nav_loaded) {
        dataf_destroy(&navla_full);
        dataf_destroy(&navlo_full);
    }
    free_cpt_data(cptdata);
    
    if (expr_mode) {
        for (int i = 1; i <= 16; i++) {
            if (channels[i].fdata.data_in || channels[i].bdata.data_in) {
                datanc_destroy(&channels[i]);
            }
        }
        for (int i = 0; i < num_required_channels; i++) {
            if (required_channels[i]) free(required_channels[i]);
        }
    }
    
    datanc_destroy(&c01);
    dataf_destroy(&result_data);
    image_destroy(&final_image);
    color_array_destroy(color_array);
    if (generated_filename) free(generated_filename);
    if (palette_name) free(palette_name);
    
    return status;
}
