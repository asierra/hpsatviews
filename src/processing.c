/* Single-channel processing pipeline (grayscale and pseudocolor).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
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
// PIPELINE v2.0: ProcessConfig + MetadataContext
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
    
    // Register basic parameters in metadata.
    metadata_set_command(meta, cfg->command);
    if (fabsf(cfg->gamma[0] - 1.0f) > 1e-6f) metadata_add(meta, "gamma", cfg->gamma[0]);
    if (cfg->apply_clahe) metadata_add_bool(meta, "clahe", true);
    if (cfg->apply_histogram) metadata_add_bool(meta, "histogram", true);
    if (cfg->invert_values) metadata_add_bool(meta, "invert", true);
    if (cfg->do_reprojection && !cfg->save_both) metadata_add_bool(meta, "geographics", true);
    if (cfg->scale != 1) metadata_add(meta, "scale", cfg->scale);
    
    // --- Modo pseudocolor: cargar paleta ---
    ColormapMeta colormap_meta = {0};

    if (is_pseudocolor) {
        metadata_add(meta, "palette", cfg->palette_file);
        cptdata = read_cpt_file(cfg->palette_file);
        if (cptdata) {
            color_array = cpt_to_color_array(cptdata);
            colormap_meta.val_min    = cptdata->entries[0].value;
            colormap_meta.val_max    = cptdata->entries[cptdata->entry_count - 1].value;
            colormap_meta.num_colors = cptdata->is_discrete ? (int)cptdata->entry_count : 256;
            colormap_meta.units      = cptdata->units[0] ? cptdata->units : NULL;
            if (cptdata->has_nan_color) {
                // Mismo índice reservado que create_single_gray() usa para NonData.
                colormap_meta.has_nodata   = true;
                colormap_meta.nodata_index = (int)cptdata->num_colors - 1;
            }
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
    
    // --- Parse --minmax if provided (applies to all modes) ---
    if (cfg->custom_minmax) {
        if (sscanf(cfg->custom_minmax, "%f,%f", &minmax[0], &minmax[1]) == 2) {
            minmax_provided = true;
            LOG_INFO("Rango de salida especificado: [%.2f, %.2f]", minmax[0], minmax[1]);
        }
    }

    // --- Band algebra (expr) mode ---
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
        
        // Load multiple channels.
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
        find_scan_mode_from_name(basename_input, cset->scan_mode, sizeof(cset->scan_mode));
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
        
        // Evaluate the band algebra expression.
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
        if (!minmax_provided) {
            minmax[0] = c01.fdata.data_in ? c01.fdata.fmin : 0.0f;
            minmax[1] = c01.fdata.data_in ? c01.fdata.fmax : 255.0f;
        }
    }
    
    // Poblar metadatos desde DataNC
    metadata_from_nc(meta, &c01);
    if (c01.product_name) metadata_set_product(meta, c01.product_name);
    if (cfg->has_clip)
        metadata_set_clip(meta, true);
    
    // Generate output filename if not specified.
    const char* outfn = cfg->output_path_override;
    
    if (!outfn) {
        // Determine file extension.
        const char* ext = (cfg->force_geotiff) ? ".tif" : ".png";
        generated_filename = metadata_build_filename(meta, ext);
        outfn = generated_filename;
        if (!outfn) {
            LOG_ERROR("No se pudo generar nombre de archivo");
            goto cleanup;
        }
    }
    
    LOG_INFO("Archivo de salida: %s", outfn);
    
    // Load navigation if needed for clip, GeoTIFF, or reprojection.
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
    if (fabsf(cfg->gamma[1] - cfg->gamma[0]) > 1e-6f || fabsf(cfg->gamma[2] - cfg->gamma[0]) > 1e-6f) {
        LOG_WARN("Modo canal único: se usará solo gamma[0]=%.2f (ignorando gamma[1]=%.2f, gamma[2]=%.2f)",
                 cfg->gamma[0], cfg->gamma[1], cfg->gamma[2]);
    }
    if (fabsf(cfg->gamma[0] - 1.0f) > 1e-6f && c01.is_float && c01.fdata.data_in) {
        LOG_INFO("Aplicando gamma %.2f", cfg->gamma[0]);
        float gmin = minmax_provided ? minmax[0] : c01.fdata.fmin;
        float gmax = minmax_provided ? minmax[1] : c01.fdata.fmax;
        dataf_apply_gamma(&c01.fdata, cfg->gamma[0], gmin, gmax);
        // After gamma, data range is [0, 1].
        minmax[0] = c01.fdata.fmin;
        minmax[1] = c01.fdata.fmax;
    }

	// Crear imagen inicial intacta
    if (c01.is_float) {
        final_image = create_single_gray(c01.fdata, cfg->invert_values, cfg->use_alpha, minmax[0], minmax[1], is_pseudocolor ? cptdata : NULL);
    } else {
        final_image = create_single_grayb(c01.bdata, cfg->invert_values, cfg->use_alpha, is_pseudocolor ? cptdata : NULL);
    }
    
    if (!final_image.data) {
        LOG_ERROR("Fallo al crear imagen");
        goto cleanup;
    }

    // Aplicar mejoras radiométricas (solo para escala de grises, el pseudocolor no debe alterarse)
    if (!is_pseudocolor) {
        if (cfg->apply_histogram) image_apply_histogram(final_image);
        if (cfg->apply_clahe)
            image_apply_clahe(final_image, cfg->clahe_tiles_x, cfg->clahe_tiles_y, cfg->clahe_clip_limit);
    }

    int final_w = final_image.width;
    int final_h = final_image.height;

    // ========================================================================
    // 1. FLUJO FIXED GRID (Se ejecuta si se pide -B, o si NO hay reproyección)
    // ========================================================================
    if (cfg->save_both || !cfg->do_reprojection) {
        ImageData fg_base = final_image;
        bool fg_base_allocated = false;
        unsigned crop_x = 0, crop_y = 0;

        // Recorte local para fixed grid
        if (cfg->has_clip && nav_loaded) {
            int ix, iy, iw, ih;
            reprojection_find_bounding_box(&navla_full, &navlo_full, 
                cfg->clip_coords[0], cfg->clip_coords[1], 
                cfg->clip_coords[2], cfg->clip_coords[3], 
                &ix, &iy, &iw, &ih);
            
            fg_base = image_crop(&final_image, ix, iy, iw, ih);
            fg_base_allocated = true;
            crop_x = (unsigned)ix;
            crop_y = (unsigned)iy;
        }

        // Remuestreo local
        ImageData fg_final = fg_base;
        bool fg_final_allocated = false;
        if (cfg->scale != 1) {
            if (cfg->scale < 0) fg_final = image_downsample_boxfilter(&fg_base, -cfg->scale);
            else fg_final = image_upsample_bilinear(&fg_base, cfg->scale);
            fg_final_allocated = true;
        }

        LOG_INFO("Guardando fixed-grid: %s", outfn);

        if (is_geotiff) {
            DataNC meta_fg = c01;
            meta_fg.geotransform[0] += crop_x * meta_fg.geotransform[1];
            meta_fg.geotransform[3] += crop_y * meta_fg.geotransform[5];

            if (cfg->scale != 1) {
                double sf = (cfg->scale < 0) ? -(double)cfg->scale : (double)cfg->scale;
                if (cfg->scale > 1) { meta_fg.geotransform[1] /= sf; meta_fg.geotransform[5] /= sf; }
                else { meta_fg.geotransform[1] *= sf; meta_fg.geotransform[5] *= sf; }
            }

            if (is_pseudocolor && color_array) {
                if (cfg->use_alpha) {
                    temp_image = image_expand_palette(&fg_final, color_array);
                    write_geotiff_rgb(outfn, &temp_image, &meta_fg, 0, 0, meta_fg.product_name);
                    image_destroy(&temp_image);
                } else {
                    write_geotiff_indexed(outfn, &fg_final, color_array, &meta_fg, 0, 0, &colormap_meta, meta_fg.product_name);
                }
            } else {
                write_geotiff_gray(outfn, &fg_final, &meta_fg, 0, 0, meta_fg.product_name);
            }
        } else {
            if (is_pseudocolor && color_array) writer_save_png_palette(outfn, &fg_final, color_array);
            else writer_save_png(outfn, &fg_final);
        }

        // Registrar geometría de fixed grid en los metadatos (si es el paso final)
        if (nav_loaded && !cfg->do_reprojection) {
            double *gt = c01.geotransform;
            double h = (c01.proj_info.valid) ? c01.proj_info.sat_height : 35786023.0;
            double x_min = (gt[0] + crop_x * gt[1]) * h;
            double y_top = (gt[3] + crop_y * gt[5]) * h;
            double x_max = x_min + (fg_final.width * gt[1] * h);
            double y_bot = y_top + (fg_final.height * gt[5] * h);
            
            double y_min = (y_bot < y_top) ? y_bot : y_top;
            double y_max_val = (y_bot > y_top) ? y_bot : y_top;
            
            metadata_set_geometry(meta, (float)x_min, (float)y_min, (float)x_max, (float)y_max_val);
            const char* sat_crs = "geostationary";
            if (c01.sat_id == SAT_GOES16) sat_crs = "goes16";
            else if (c01.sat_id == SAT_GOES17) sat_crs = "goes17";
            else if (c01.sat_id == SAT_GOES18) sat_crs = "goes18";
            else if (c01.sat_id == SAT_GOES19) sat_crs = "goes19";
            metadata_set_projection(meta, sat_crs);
        }

        final_w = fg_final.width;
        final_h = fg_final.height;

        if (fg_final_allocated) image_destroy(&fg_final);
        if (fg_base_allocated) image_destroy(&fg_base);
    }

    // ========================================================================
    // 2. FLUJO GEOGRÁFICO / REPROYECCIÓN (Se ejecuta si se pide -G o -B)
    // ========================================================================
    if (cfg->do_reprojection) {
        if (!nav_loaded) {
            LOG_ERROR("Navegación requerida para reproyección");
            goto cleanup;
        }

        // Patrón a escribir en las áreas de la malla geográfica que quedan
        // fuera del disco visible/los límites de la fuente (rejilla
        // rectangular vs. disco redondo): debe verse como NonData, no como
        // dato real.
        unsigned char nodata_pattern[4] = {0};
        const unsigned char *nodata_pixel = NULL;
        if (cfg->use_alpha) {
            nodata_pixel = nodata_pattern; // valor=0, alpha=0 (ya es la convención de NonData)
        } else if (colormap_meta.has_nodata) {
            nodata_pattern[0] = (unsigned char)colormap_meta.nodata_index;
            nodata_pixel = nodata_pattern;
        } else if (is_pseudocolor) {
            LOG_WARN("Reproyección pseudocolor sin --alpha y sin color N en el .cpt: "
                     "las áreas fuera del disco no podrán distinguirse de dato real.");
        }

        // Reproyectar usando la imagen original inalterada
        ImageData geo_base = reproject_image_analytical(
            &final_image, &c01,
            navla_full.fmin, navla_full.fmax,
            navlo_full.fmin, navlo_full.fmax,
            c01.native_resolution_km,
            cfg->has_clip ? cfg->clip_coords : NULL,
            nodata_pixel
        );

        float final_lon_min, final_lon_max, final_lat_min, final_lat_max;
        if (cfg->has_clip) {
            final_lon_min = cfg->clip_coords[0]; final_lat_max = cfg->clip_coords[1];
            final_lon_max = cfg->clip_coords[2]; final_lat_min = cfg->clip_coords[3];
        } else {
            final_lon_min = navlo_full.fmin; final_lon_max = navlo_full.fmax;
            final_lat_min = navla_full.fmin; final_lat_max = navla_full.fmax;
        }

        // Remuestreo local
        ImageData geo_final = geo_base;
        bool geo_final_allocated = false;
        if (cfg->scale != 1) {
            if (cfg->scale < 0) geo_final = image_downsample_boxfilter(&geo_base, -cfg->scale);
            else geo_final = image_upsample_bilinear(&geo_base, cfg->scale);
            geo_final_allocated = true;
        }

        // Si venimos del flujo -B, alteramos el nombre para añadir _geo
        char *final_outfn = (char*)outfn;
        if (cfg->save_both) {
            final_outfn = insert_geo_suffix(outfn);
            if (generated_filename) free(generated_filename);
            generated_filename = final_outfn;
            outfn = generated_filename;
        }

        LOG_INFO("Guardando reproyectado: %s", outfn);

        if (is_geotiff) {
            DataNC meta_out = c01;
            meta_out.proj_code = PROJ_LATLON;
            meta_out.proj_info.valid = false;
            meta_out.geotransform[0] = final_lon_min;
            meta_out.geotransform[1] = (final_lon_max - final_lon_min) / (double)geo_final.width;
            meta_out.geotransform[2] = 0.0;
            meta_out.geotransform[3] = final_lat_max;
            meta_out.geotransform[4] = 0.0;
            meta_out.geotransform[5] = (final_lat_min - final_lat_max) / (double)geo_final.height;

            if (is_pseudocolor && color_array) {
                if (cfg->use_alpha) {
                    temp_image = image_expand_palette(&geo_final, color_array);
                    write_geotiff_rgb(outfn, &temp_image, &meta_out, 0, 0, meta_out.product_name);
                    image_destroy(&temp_image);
                } else {
                    write_geotiff_indexed(outfn, &geo_final, color_array, &meta_out, 0, 0, &colormap_meta, meta_out.product_name);
                }
            } else {
                write_geotiff_gray(outfn, &geo_final, &meta_out, 0, 0, meta_out.product_name);
            }
        } else {
            if (is_pseudocolor && color_array) writer_save_png_palette(outfn, &geo_final, color_array);
            else writer_save_png(outfn, &geo_final);
        }

        metadata_set_geometry(meta, final_lon_min, final_lat_min, final_lon_max, final_lat_max);
        metadata_set_projection(meta, "EPSG:4326");

        final_w = geo_final.width;
        final_h = geo_final.height;

        if (geo_final_allocated) image_destroy(&geo_final);
        image_destroy(&geo_base);
    }

    metadata_add(meta, "output_file", outfn);
    metadata_add(meta, "output_width", final_w);
    metadata_add(meta, "output_height", final_h);
    
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
