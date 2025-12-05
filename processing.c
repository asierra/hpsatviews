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
#include "writer_geotiff.h"
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
    LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
    logger_init(log_level);
    LOG_DEBUG("Modo verboso activado para el comando de procesamiento.");

    char *fnc01;
    if (ap_has_args(parser)) {
        fnc01 = ap_get_arg_at_index(parser, 0);
    } else {
        LOG_ERROR("Debe proporcionar un archivo NetCDF de entrada.");
        return -1;
    }

    if (is_pseudocolor && !ap_found(parser, "cpt")) {
        LOG_ERROR("El comando 'pseudocolor' requiere una paleta de colores. Use la opción -p/--cpt.");
        return -1;
    }

    // --- Opciones de procesamiento ---
    const bool invert_values = ap_found(parser, "invert");
    const bool apply_histogram = ap_found(parser, "histo");
    const bool use_alpha = ap_found(parser, "alpha");
    const float gamma = ap_get_dbl_value(parser, "gamma");
    const int scale = ap_get_int_value(parser, "scale");
    const bool do_reprojection = ap_found(parser, "geographics");
    const bool force_geotiff = ap_found(parser, "geotiff");

    // --- Nombre de archivo de salida ---
    char* out_filename_generated = NULL;
    const char* outfn;
    if (ap_found(parser, "out")) {
        outfn = ap_get_str_value(parser, "out");
    } else {
        const char* mode_name = is_pseudocolor ? "pseudocolor" : "singlegray";
        const char* extension = force_geotiff ? ".tif" : ".png";
        char* base_filename = generate_default_output_filename(fnc01, mode_name, extension);
        
        if (do_reprojection && base_filename) {
            char* dot = strrchr(base_filename, '.');
            if (dot) {
                size_t prefix_len = dot - base_filename;
                size_t total_len = strlen(base_filename) + 5; // "_geo\0"
                out_filename_generated = (char*)malloc(total_len);
                if (out_filename_generated) {
                    strncpy(out_filename_generated, base_filename, prefix_len);
                    out_filename_generated[prefix_len] = '\0';
                    strcat(out_filename_generated, "_geo");
                    strcat(out_filename_generated, dot);
                    free(base_filename);
                } else {
                    out_filename_generated = base_filename; // Fallback
                }
            } else {
                out_filename_generated = base_filename;
            }
        } else {
            out_filename_generated = base_filename;
        }
        outfn = out_filename_generated;
    }

    // --- Carga de datos ---
    CPTData* cptdata = NULL;
    ColorArray *color_array = NULL;
    if (is_pseudocolor) {
        char *cptfn = ap_get_str_value(parser, "cpt");
        cptdata = read_cpt_file(cptfn);
        if (cptdata) {
            color_array = cpt_to_color_array(cptdata);
        } else {
            LOG_ERROR("No se pudo cargar el archivo de paleta: %s", cptfn);
            if (out_filename_generated) free(out_filename_generated);
            return -1;
        }
    }
    
    DataNC c01;
    char *varname = "Rad"; // Default
    if (strinstr(fnc01, "CMIP")) varname = "CMI"; else if (strinstr(fnc01, "LST")) varname = "LST";
    else if (strinstr(fnc01, "ACTP")) varname = "Phase"; else if (strinstr(fnc01, "CTP")) varname = "PRES";
    if (load_nc_sf(fnc01, varname, &c01) != 0) {
        LOG_ERROR("No se pudo cargar el archivo NetCDF: %s", fnc01);
        if (out_filename_generated) free(out_filename_generated);
        free_cpt_data(cptdata);
        color_array_destroy(color_array);
        return -1;
    }

    // --- Navegación y Recorte ---
    DataF navla = {0}, navlo = {0};
    bool has_clip = ap_found(parser, "clip") && ap_count(parser, "clip") == 4;
    bool nav_loaded = false;
    bool is_geotiff = force_geotiff || (outfn && (strstr(outfn, ".tif") || strstr(outfn, ".tiff")));

    if (has_clip || is_geotiff || do_reprojection) {
        if (compute_navigation_nc(fnc01, &navla, &navlo) == 0) {
            nav_loaded = true;
        } else {
            LOG_WARN("No se pudo cargar la navegación del archivo NetCDF.");
            if (is_geotiff) {
                LOG_ERROR("La navegación es requerida para GeoTIFF. Abortando.");
                // Cleanup and exit
                if (out_filename_generated) free(out_filename_generated);
                free_cpt_data(cptdata);
                datanc_destroy(&c01);
                color_array_destroy(color_array);
                return -1;
            }
        }
    }

    float clip_coords[4] = {0};
    if (has_clip) {
        for(int i=0; i<4; i++) clip_coords[i] = atof(ap_get_str_value_at_index(parser, "clip", i));
    }
    
    // --- Procesamiento de imagen ---
    ImageData imout = {0};
    if (c01.is_float) {
        if (do_reprojection && has_clip && nav_loaded) {
            LOG_INFO("Aplicando recorte PRE-reproyección: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                     clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);
            int x, y, w, h, vs;
            vs = reprojection_find_bounding_box(&navla, &navlo, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3], &x, &y, &w, &h);
            if (vs >= 4 && w > 0 && h > 0) {
                DataF d = dataf_crop(&c01.fdata, x, y, w, h);
                DataF la = dataf_crop(&navla, x, y, w, h);
                DataF lo = dataf_crop(&navlo, x, y, w, h);
                dataf_destroy(&c01.fdata); dataf_destroy(&navla); dataf_destroy(&navlo);
                c01.fdata = d; navla = la; navlo = lo;
            }
        }
        
        if (do_reprojection) {
            LOG_INFO("Iniciando reproyección...");
            float lon_min, lon_max, lat_min, lat_max;
            DataF reprojected_data = (has_clip && nav_loaded)
                ? reproject_to_geographics_with_nav(&c01.fdata, &navla, &navlo, c01.native_resolution_km, &lon_min, &lon_max, &lat_min, &lat_max)
                : reproject_to_geographics(&c01.fdata, fnc01, c01.native_resolution_km, &lon_min, &lon_max, &lat_min, &lat_max);

            if (reprojected_data.data_in) {
                dataf_destroy(&c01.fdata);
                c01.fdata = reprojected_data;
                
                dataf_destroy(&navla);
                dataf_destroy(&navlo);
                
                // Si hay clip, recortar la imagen reproyectada a los límites exactos del clip
                if (has_clip) {
                    // Los límites actuales (lon_min, lon_max, etc.) son del bounding box
                    // Queremos recortar a los límites exactos del clip
                    float clip_lon_min = clip_coords[0];
                    float clip_lon_max = clip_coords[2];
                    float clip_lat_min = clip_coords[3];
                    float clip_lat_max = clip_coords[1];
                    
                    // Calcular qué porción de la imagen corresponde al clip
                    float lon_range = lon_max - lon_min;
                    float lat_range = lat_max - lat_min;
                    
                    int x_start = (int)((clip_lon_min - lon_min) / lon_range * c01.fdata.width);
                    int y_start = (int)((lat_max - clip_lat_max) / lat_range * c01.fdata.height);
                    int x_end = (int)((clip_lon_max - lon_min) / lon_range * c01.fdata.width);
                    int y_end = (int)((lat_max - clip_lat_min) / lat_range * c01.fdata.height);
                    
                    // Asegurar que están dentro de los límites
                    if (x_start < 0) x_start = 0;
                    if (y_start < 0) y_start = 0;
                    if (x_end > (int)c01.fdata.width) x_end = c01.fdata.width;
                    if (y_end > (int)c01.fdata.height) y_end = c01.fdata.height;
                    
                    int crop_w = x_end - x_start;
                    int crop_h = y_end - y_start;
                    
                    if (crop_w > 0 && crop_h > 0) {
                        LOG_INFO("Recortando imagen reproyectada a límites exactos del clip: [%d,%d] %dx%d", 
                                 x_start, y_start, crop_w, crop_h);
                        DataF cropped = dataf_crop(&c01.fdata, x_start, y_start, crop_w, crop_h);
                        dataf_destroy(&c01.fdata);
                        c01.fdata = cropped;
                        
                        // Actualizar límites a los del clip exacto
                        lon_min = clip_lon_min;
                        lon_max = clip_lon_max;
                        lat_min = clip_lat_min;
                        lat_max = clip_lat_max;
                    }
                }
                
                create_navigation_from_reprojected_bounds(&navla, &navlo, c01.fdata.width, c01.fdata.height, lon_min, lon_max, lat_min, lat_max);
                nav_loaded = true; // Navigation is now the new grid
            }
        }

        if (scale < 0) {
            DataF aux = downsample_boxfilter(c01.fdata, -scale); dataf_destroy(&c01.fdata); c01.fdata = aux;
        } else if (scale > 1) {
            DataF aux = upsample_bilinear(c01.fdata, scale); dataf_destroy(&c01.fdata); c01.fdata = aux;
        }
        imout = create_single_gray(c01.fdata, invert_values, use_alpha, cptdata);
    } else {
        if (do_reprojection || scale != 1) LOG_WARN("Reproyección/escalado no implementado para datos byte. Opciones ignoradas.");
        imout = create_single_grayb(c01.bdata, invert_values, use_alpha, cptdata);
    }

    if (gamma != 1.0) image_apply_gamma(imout, gamma);
    if (apply_histogram) image_apply_histogram(imout);

    // Guardar índices del crop para GeoTIFF geoestacionario
    unsigned crop_x_start = 0, crop_y_start = 0;
    int offset_x_pixels = 0, offset_y_pixels = 0;
    
    if (has_clip && nav_loaded) {
        int ix, iy, iw, ih;
        if (do_reprojection) {
            float lon_range = navlo.fmax - navlo.fmin, lat_range = navla.fmax - navla.fmin;
            ix = (int)(((clip_coords[0] - navlo.fmin) / lon_range) * imout.width);
            iy = (int)(((navla.fmax - clip_coords[1]) / lat_range) * imout.height);
            int ix_end = (int)(((clip_coords[2] - navlo.fmin) / lon_range) * imout.width);
            int iy_end = (int)(((navla.fmax - clip_coords[3]) / lat_range) * imout.height);
            ix = (ix < 0) ? 0 : ix; iy = (iy < 0) ? 0 : iy;
            iw = ((ix_end > ix) ? (ix_end - ix) : 0); ih = ((iy_end > iy) ? (iy_end - iy) : 0);
        } else {
            // Para caso geoestacionario: usar bounding box porque la navegación puede tener píxeles inválidos
            reprojection_find_bounding_box(&navla, &navlo, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3], &ix, &iy, &iw, &ih);
        }
        
        LOG_INFO("Crop%s: start[%d,%d], size[%d,%d]", 
                 do_reprojection ? " fino (reproyectado)" : " (geoestacionario bbox)", ix, iy, iw, ih);
        
        crop_x_start = (unsigned)ix;
        crop_y_start = (unsigned)iy;
        ImageData cropped = image_crop(&imout, ix, iy, iw, ih);
        if (cropped.data) { image_destroy(&imout); imout = cropped; }
        
        // Recortar también la navegación para que coincida con la imagen recortada
        DataF navla_cropped = dataf_crop(&navla, ix, iy, iw, ih);
        DataF navlo_cropped = dataf_crop(&navlo, ix, iy, iw, ih);
        
        LOG_INFO("Navegación recortada: lat[%.3f, %.3f], lon[%.3f, %.3f]",
                 navla_cropped.fmin, navla_cropped.fmax, navlo_cropped.fmin, navlo_cropped.fmax);
        LOG_INFO("Centro navegación: lat=%.3f, lon=%.3f",
                 (navla_cropped.fmin + navla_cropped.fmax) / 2.0,
                 (navlo_cropped.fmin + navlo_cropped.fmax) / 2.0);
        
        // Para caso geoestacionario: ajustar crop_x_start/crop_y_start para que correspondan
        // al centro real del clip, no al centro del bounding box
        if (!do_reprojection && has_clip) {
            // Calcular el centro del clip deseado
            float clip_lat_center = (clip_coords[1] + clip_coords[3]) / 2.0f;  // (lat_max + lat_min) / 2
            float clip_lon_center = (clip_coords[0] + clip_coords[2]) / 2.0f;  // (lon_min + lon_max) / 2
            
            // Calcular el centro del bounding box actual (en coordenadas de navegación)
            float nav_lat_center = (navla_cropped.fmin + navla_cropped.fmax) / 2.0f;
            float nav_lon_center = (navlo_cropped.fmin + navlo_cropped.fmax) / 2.0f;
            
            // Calcular el offset en píxeles desde el centro del bbox hasta el centro del clip
            float lat_range_bbox = navla_cropped.fmax - navla_cropped.fmin;
            float lon_range_bbox = navlo_cropped.fmax - navlo_cropped.fmin;
            
            // Offset en fracción de la imagen
            float lat_offset_frac = (nav_lat_center - clip_lat_center) / lat_range_bbox;
            float lon_offset_frac = (clip_lon_center - nav_lon_center) / lon_range_bbox;
            
            // Convertir a píxeles
            offset_x_pixels = (int)(lon_offset_frac * iw);
            offset_y_pixels = (int)(lat_offset_frac * ih);
            
            LOG_INFO("Ajuste centro bbox→clip: offset_píxeles[%d,%d], offset_grados[%.3f°lon, %.3f°lat]",
                     offset_x_pixels, offset_y_pixels, clip_lon_center - nav_lon_center, clip_lat_center - nav_lat_center);
            
            // NO ajustamos crop_x_start/crop_y_start aquí
            // Los índices del bbox se usarán para leer coordenadas x[],y[] del NetCDF
            // El offset se guardará en el contexto y se aplicará al GeoTransform
            LOG_INFO("Bbox original para leer coordenadas: crop[%u,%u]", crop_x_start, crop_y_start);
            LOG_INFO("El offset [%d,%d] píxeles se aplicará al GeoTransform", offset_x_pixels, offset_y_pixels);
        }
        
        dataf_destroy(&navla);
        dataf_destroy(&navlo);
        navla = navla_cropped;
        navlo = navlo_cropped;
    }
    
    // --- Escritura de archivo ---
    if (is_geotiff) {
        if (is_pseudocolor && color_array) {
            write_geotiff_indexed(outfn, &imout, color_array, &navla, &navlo, fnc01, do_reprojection, crop_x_start, crop_y_start, offset_x_pixels, offset_y_pixels);
        } else {
            write_geotiff_gray(outfn, &imout, &navla, &navlo, fnc01, do_reprojection, crop_x_start, crop_y_start, offset_x_pixels, offset_y_pixels);
        }
    } else {
        if (is_pseudocolor && color_array) {
            write_image_png_palette(outfn, &imout, color_array);
        } else {
            write_image_png(outfn, &imout);
        }
    }
    LOG_INFO("Imagen guardada en: %s", outfn);

    // --- Liberación de memoria ---
    if (nav_loaded) {
        dataf_destroy(&navla);
        dataf_destroy(&navlo);
    }
    free_cpt_data(cptdata);
    datanc_destroy(&c01);
    image_destroy(&imout);
    color_array_destroy(color_array);
    if (out_filename_generated) free(out_filename_generated);

    return 0;
}