/*
 * Single-channel processing module (singlegray and pseudocolor)
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "processing.h"
#include "args.h"
#include "logger.h"
#include "reader_nc.h"
#include "reader_cpt.h"
#include "writer_png.h"
#include "filename_utils.h"
#include "reprojection.h"
#include "image.h"
#include "datanc.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha, const CPTData* cpt);
ImageData create_single_grayb(DataB c01, bool invert_value, bool use_alpha, const CPTData* cpt);

bool strinstr(const char *main_str, const char *sub) {
    return (main_str && sub && strstr(main_str, sub));
}

int run_processing(ArgParser *parser, bool is_pseudocolor) {
    // Inicializar el logger aquí para que los mensajes DEBUG se muestren
    LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
    logger_init(log_level);
    LOG_DEBUG("Modo verboso activado para el comando de procesamiento.");

    char *fnc01;
    bool invert_values = false;
    bool apply_histogram = false;
    bool use_alpha = false;
    bool do_reprojection = false;
    float gamma = 1.0;
    int scale = 1;

    if (is_pseudocolor && !ap_found(parser, "cpt")) {
        LOG_ERROR("El comando 'pseudocolor' requiere una paleta de colores. Use la opción -p/--cpt.");
        return -1;
    }

    if (ap_has_args(parser)) {
        fnc01 = ap_get_arg_at_index(parser, 0);
    } else {
        LOG_ERROR("Debe proporcionar un archivo NetCDF de entrada.");
        return -1;
    }

    char* out_filename_generated = NULL;
    const char* outfn;
    
    // Primero necesitamos saber si hay reproyección para el nombre de archivo
    do_reprojection = ap_found(parser, "geographics");

    if (ap_found(parser, "out")) {
        outfn = ap_get_str_value(parser, "out");
    } else {
        // Generar nombre de archivo por defecto si no fue proporcionado
        const char* mode_name = is_pseudocolor ? "pseudocolor" : "singlegray";
        char* base_filename = generate_default_output_filename(fnc01, mode_name, ".png");
        
        // Si hay reproyección, agregar sufijo _geo antes de la extensión
        if (do_reprojection && base_filename) {
            char* dot = strrchr(base_filename, '.');
            if (dot) {
                size_t prefix_len = dot - base_filename;
                size_t total_len = strlen(base_filename) + 5;
                out_filename_generated = (char*)malloc(total_len);
                if (out_filename_generated) {
                    strncpy(out_filename_generated, base_filename, prefix_len);
                    out_filename_generated[prefix_len] = '\0';
                    strcat(out_filename_generated, "_geo");
                    strcat(out_filename_generated, dot);
                    free(base_filename);
                } else {
                    out_filename_generated = base_filename;
                }
            } else {
                out_filename_generated = base_filename;
            }
        } else {
            out_filename_generated = base_filename;
        }
        
        outfn = out_filename_generated;
    }


    invert_values = ap_found(parser, "invert");
    apply_histogram = ap_found(parser, "histo");
    use_alpha = ap_found(parser, "alpha");
    gamma = ap_get_dbl_value(parser, "gamma");
    scale = ap_get_int_value(parser, "scale");

    CPTData* cptdata = NULL;
    ColorArray *color_array = NULL;
    if (is_pseudocolor) {
        if (ap_found(parser, "cpt")) {
            char *cptfn = ap_get_str_value(parser, "cpt");
            cptdata = read_cpt_file(cptfn);
            if (cptdata) {
                color_array = cpt_to_color_array(cptdata);
            } else {
                LOG_ERROR("No se pudo cargar el archivo de paleta: %s", cptfn);
                return -1;
            }
        }
    }
    
    DataNC c01;
    char *varname = "Rad"; // Default
    if (strinstr(fnc01, "CMIP")) varname = "CMI";
    else if (strinstr(fnc01, "LST")) varname = "LST";
    else if (strinstr(fnc01, "ACTP")) varname = "Phase";
    else if (strinstr(fnc01, "CTP")) varname = "PRES";
    if (load_nc_sf(fnc01, varname, &c01) != 0) {
        LOG_ERROR("No se pudo cargar el archivo NetCDF: %s", fnc01);
        free_cpt_data(cptdata);
        color_array_destroy(color_array);
        return -1;
    }

    // Variables para navegación y recorte PRE-reproyección
    DataF navla, navlo;
    bool has_clip = false;
    bool nav_loaded = false;
    float clip_lon_min = 0, clip_lat_max = 0, clip_lon_max = 0, clip_lat_min = 0;
    
    // Si hay clip, cargar navegación primero
    if (ap_found(parser, "clip") && ap_count(parser, "clip") == 4) {
        clip_lon_min = atof(ap_get_str_value_at_index(parser, "clip", 0));
        clip_lat_max = atof(ap_get_str_value_at_index(parser, "clip", 1));
        clip_lon_max = atof(ap_get_str_value_at_index(parser, "clip", 2));
        clip_lat_min = atof(ap_get_str_value_at_index(parser, "clip", 3));
        has_clip = true;
        
        if (compute_navigation_nc(fnc01, &navla, &navlo) == 0) {
            nav_loaded = true;
        } else {
            LOG_WARN("No se pudo cargar la navegación para el recorte. Ignorando --clip.");
            has_clip = false;
        }
    }

    ImageData imout;

    if (c01.is_float) {
        // --- RUTA PARA DATOS FLOAT ---
        
        // Si tenemos clip + reprojection, hacer clip PRE-reproyección
        if (do_reprojection && has_clip && nav_loaded) {
            // Hacer recorte en proyección nativa PRIMERO
            LOG_INFO("Aplicando recorte PRE-reproyección: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                     clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);
            
            // Usar función compartida de muestreo denso
            int clip_x_start, clip_y_start, clip_width, clip_height;
            int valid_samples = reprojection_find_bounding_box(&navla, &navlo,
                                                                 clip_lon_min, clip_lat_max,
                                                                 clip_lon_max, clip_lat_min,
                                                                 &clip_x_start, &clip_y_start,
                                                                 &clip_width, &clip_height);
            
            // Recortar datos y navegación
            if (valid_samples >= 4 && clip_width > 0 && clip_height > 0) {
                DataF clipped_data = dataf_crop(&c01.fdata, clip_x_start, clip_y_start, clip_width, clip_height);
                DataF clipped_navla = dataf_crop(&navla, clip_x_start, clip_y_start, clip_width, clip_height);
                DataF clipped_navlo = dataf_crop(&navlo, clip_x_start, clip_y_start, clip_width, clip_height);
                
                dataf_destroy(&c01.fdata);
                dataf_destroy(&navla);
                dataf_destroy(&navlo);
                
                c01.fdata = clipped_data;
                navla = clipped_navla;
                navlo = clipped_navlo;
                
                LOG_INFO("Recorte PRE-reproyección: [%d,%d] size %dx%d (desde %d muestras válidas)", 
                         clip_x_start, clip_y_start, clip_width, clip_height, valid_samples);
            }
        }
        
        if (do_reprojection) {
            DataF reprojected_data;
            if (has_clip && nav_loaded) {
                // Usar navegación ya recortada
                reprojected_data = reproject_to_geographics_with_nav(&c01.fdata, &navla, &navlo,
                                                                      c01.native_resolution_km,
                                                                      NULL, NULL, NULL, NULL);
            } else {
                // Reproyectar todo el dataset
                reprojected_data = reproject_to_geographics(&c01.fdata, fnc01, 
                                                             c01.native_resolution_km,
                                                             NULL, NULL, NULL, NULL);
            }
            if (reprojected_data.data_in) {
                dataf_destroy(&c01.fdata);
                c01.fdata = reprojected_data;
            }
        }

        if (scale < 0) {
            DataF aux = downsample_boxfilter(c01.fdata, -scale);
            dataf_destroy(&c01.fdata);
            c01.fdata = aux;
        } else if (scale > 1) {
            DataF aux = upsample_bilinear(c01.fdata, scale);
            dataf_destroy(&c01.fdata);
            c01.fdata = aux;
        }

        imout = create_single_gray(c01.fdata, invert_values, use_alpha, cptdata);
    } else {
        // --- RUTA PARA DATOS BYTE ---
        if (do_reprojection || scale != 1) {
            LOG_WARN("La reproyección y el escalado aún no están implementados para datos de tipo byte. Se ignorarán estas opciones.");
        }
        imout = create_single_grayb(c01.bdata, invert_values, use_alpha, cptdata);
    }

    if (gamma != 1.0) image_apply_gamma(imout, gamma);
    if (apply_histogram) image_apply_histogram(imout);

    // --- LÓGICA DE RECORTE POST-procesamiento ---
    if (has_clip && nav_loaded) {
        if (do_reprojection) {
            // POST-clip para reproyección: ajustar al dominio exacto solicitado
            // La navegación recortada puede tener extensión ligeramente mayor
            LOG_INFO("Aplicando POST-clip para ajustar a dominio exacto: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                     clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);
            
            // Usar interpolación lineal en la malla reproyectada
            float lon_range = navlo.fmax - navlo.fmin;
            float lat_range = navla.fmax - navla.fmin;
            
            int ix_start = (int)(((clip_lon_min - navlo.fmin) / lon_range) * (float)imout.width);
            int iy_start = (int)(((navla.fmax - clip_lat_max) / lat_range) * (float)imout.height);
            int ix_end = (int)(((clip_lon_max - navlo.fmin) / lon_range) * (float)imout.width);
            int iy_end = (int)(((navla.fmax - clip_lat_min) / lat_range) * (float)imout.height);
            
            // Asegurar que los índices estén dentro de los límites
            if (ix_start < 0) ix_start = 0;
            if (iy_start < 0) iy_start = 0;
            if (ix_end > (int)imout.width) ix_end = imout.width;
            if (iy_end > (int)imout.height) iy_end = imout.height;
            
            unsigned int crop_width = (ix_end > ix_start) ? (ix_end - ix_start) : 0;
            unsigned int crop_height = (iy_end > iy_start) ? (iy_end - iy_start) : 0;
            
            LOG_INFO("POST-clip: start[%d, %d], size[%u, %u]", ix_start, iy_start, crop_width, crop_height);
            
            ImageData cropped_image = image_crop(&imout, ix_start, iy_start, crop_width, crop_height);
            if (cropped_image.data) {
                image_destroy(&imout);
                imout = cropped_image;
            }
        } else {
            // POST-clip para caso sin reproyección
            LOG_INFO("Aplicando recorte POST-procesamiento: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                     clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);

            int ix_start, iy_start, crop_w, crop_h;
            int valid_samples = reprojection_find_bounding_box(&navla, &navlo,
                                                                 clip_lon_min, clip_lat_max,
                                                                 clip_lon_max, clip_lat_min,
                                                                 &ix_start, &iy_start,
                                                                 &crop_w, &crop_h);
            
            unsigned int crop_width, crop_height;
            if (valid_samples < 4) {
                LOG_ERROR("Dominio de clip fuera del disco visible (solo %d muestras válidas). Ignorando --clip.", valid_samples);
                crop_width = 0;
                crop_height = 0;
            } else {
                crop_width = (unsigned int)crop_w;
                crop_height = (unsigned int)crop_h;
                LOG_INFO("Bounding box desde %d muestras válidas: start[%d, %d], size[%u, %u]", 
                         valid_samples, ix_start, iy_start, crop_width, crop_height);
            }

            ImageData cropped_image = image_crop(&imout, ix_start, iy_start, crop_width, crop_height);
            if (cropped_image.data) {
                image_destroy(&imout);
                imout = cropped_image;
            }
        }
    }
    
    // Liberar navegación si fue cargada
    if (nav_loaded) {
        dataf_destroy(&navla);
        dataf_destroy(&navlo);
    }

    if (is_pseudocolor && color_array) {
        write_image_png_palette(outfn, &imout, color_array);
    } else {
        write_image_png(outfn, &imout);
    }

    LOG_INFO("Imagen guardada en: %s", outfn);

    free_cpt_data(cptdata);
    datanc_destroy(&c01);
    image_destroy(&imout);
    color_array_destroy(color_array);
    if (out_filename_generated) free(out_filename_generated);

    return 0;
}