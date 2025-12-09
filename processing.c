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
#include "clip_loader.h"
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

// --- Función helper para procesar coordenadas de clip ---
static bool process_clip_coords(ArgParser* parser, const char* clip_csv_path, float clip_coords[4]) {
    if (!ap_found(parser, "clip")) {
        return false;
    }
    
    const char* clip_value = ap_get_str_value(parser, "clip");
    if (!clip_value || strlen(clip_value) == 0) {
        return false;
    }
    
    // Intentar parsear como 4 coordenadas separadas por comas o espacios
    float coords[4];
    int parsed = sscanf(clip_value, "%f%*[, ]%f%*[, ]%f%*[, ]%f", 
                       &coords[0], &coords[1], &coords[2], &coords[3]);
    
    if (parsed == 4) {
        // Son 4 coordenadas
        for (int i = 0; i < 4; i++) {
            clip_coords[i] = coords[i];
        }
        LOG_INFO("Usando recorte con coordenadas: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                 clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);
        return true;
    }
    
    // No se pudo parsear como coordenadas, intentar como clave
    GeoClip clip = buscar_clip_por_clave(clip_csv_path, clip_value);
    
    if (!clip.encontrado) {
        LOG_ERROR("No se encontró el recorte con clave '%s' en %s", clip_value, clip_csv_path);
        LOG_ERROR("Formato esperado: clave (ej. 'mexico') o coordenadas \"lon_min,lat_max,lon_max,lat_min\"");
        return false;
    }
    
    LOG_INFO("Usando recorte '%s': %s", clip_value, clip.region);
    clip_coords[0] = clip.ul_x;  // lon_min
    clip_coords[1] = clip.ul_y;  // lat_max
    clip_coords[2] = clip.lr_x;  // lon_max
    clip_coords[3] = clip.lr_y;  // lat_min
    return true;
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
        const char* user_out = ap_get_str_value(parser, "out");
        // Detectar si hay patrón (contiene llaves)
        if (strchr(user_out, '{') && strchr(user_out, '}')) {
            out_filename_generated = expand_filename_pattern(user_out, fnc01);
            outfn = out_filename_generated;
        } else {
            outfn = user_out;
        }
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
    
    // NOTA: load_nc_sf debe llenar los metadatos de DataNC (geotransform, proj_code)
    if (load_nc_sf(fnc01, varname, &c01) != 0) {
        LOG_ERROR("No se pudo cargar el archivo NetCDF: %s", fnc01);
        if (out_filename_generated) free(out_filename_generated);
        free_cpt_data(cptdata);
        color_array_destroy(color_array);
        return -1;
    }

    // --- Navegación y Recorte ---
    DataF navla = {0}, navlo = {0};
    bool nav_loaded = false;
    bool is_geotiff = force_geotiff || (outfn && (strstr(outfn, ".tif") || strstr(outfn, ".tiff")));

    if (ap_found(parser, "clip") || is_geotiff || do_reprojection) {
        if (compute_navigation_nc(fnc01, &navla, &navlo) == 0) {
            nav_loaded = true;
        } else {
            LOG_WARN("No se pudo cargar la navegación del archivo NetCDF.");
            if (is_geotiff) {
                LOG_ERROR("La navegación es requerida para GeoTIFF. Abortando.");
                if (out_filename_generated) free(out_filename_generated);
                free_cpt_data(cptdata);
                datanc_destroy(&c01);
                color_array_destroy(color_array);
                return -1;
            }
        }
    }

    float clip_coords[4] = {0};
    bool has_clip = process_clip_coords(parser, "/usr/local/share/lanot/docs/recortes_coordenadas.csv", clip_coords);
    
    // --- Procesamiento de imagen ---
    ImageData imout = {0};
    
    // Variables para metadatos de salida
    float final_lon_min = 0, final_lon_max = 0, final_lat_min = 0, final_lat_max = 0;

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
            DataF reprojected_data;
            if (has_clip && nav_loaded) {
                // Reproyectar directamente al dominio del clip (no necesita recorte post-reproyección)
                reprojected_data = reproject_to_geographics_with_nav(&c01.fdata, &navla, &navlo, 
                                                                     c01.native_resolution_km, 
                                                                     &final_lon_min, &final_lon_max, 
                                                                     &final_lat_min, &final_lat_max,
                                                                     clip_coords);
            } else {
                // Sin clip, usar límites de navegación
                reprojected_data = reproject_to_geographics(&c01.fdata, fnc01, 
                                                            c01.native_resolution_km, 
                                                            &final_lon_min, &final_lon_max, 
                                                            &final_lat_min, &final_lat_max);
            }

            if (reprojected_data.data_in) {
                dataf_destroy(&c01.fdata);
                c01.fdata = reprojected_data;
                
                dataf_destroy(&navla);
                dataf_destroy(&navlo);
                
                create_navigation_from_reprojected_bounds(&navla, &navlo, c01.fdata.width, c01.fdata.height, final_lon_min, final_lon_max, final_lat_min, final_lat_max);
                nav_loaded = true;
            }
        }

        imout = create_single_gray(c01.fdata, invert_values, use_alpha, cptdata);
    } else {
        // Datos BYTE
        if (do_reprojection || scale != 1) LOG_WARN("Reproyección/escalado no implementado para datos byte. Opciones ignoradas.");
        imout = create_single_grayb(c01.bdata, invert_values, use_alpha, cptdata);
    }

    if (gamma != 1.0) image_apply_gamma(imout, gamma);
    if (apply_histogram) image_apply_histogram(imout);
    
    // --- REMUESTREO (si se solicitó) ---
    if (scale < 0) {
        ImageData scaled = image_downsample_boxfilter(&imout, -scale);
        image_destroy(&imout);
        imout = scaled;
    } else if (scale > 1) {
        ImageData scaled = image_upsample_bilinear(&imout, scale);
        image_destroy(&imout);
        imout = scaled;
    }

    // Guardar índices del crop para GeoTIFF geoestacionario
    unsigned crop_x_start = 0, crop_y_start = 0;
    
    // Si no estamos reproyectando y hay recorte, aplicamos recorte a la imagen final
    if (has_clip && nav_loaded && !do_reprojection) {
        int ix, iy, iw, ih;
        reprojection_find_bounding_box(&navla, &navlo, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3], &ix, &iy, &iw, &ih);
        
        LOG_INFO("Crop geoestacionario: start[%d,%d], size[%d,%d]", ix, iy, iw, ih);
        
        crop_x_start = (unsigned)ix;
        crop_y_start = (unsigned)iy;
        ImageData cropped = image_crop(&imout, ix, iy, iw, ih);
        if (cropped.data) { image_destroy(&imout); imout = cropped; }
        
        DataF navla_cropped = dataf_crop(&navla, ix, iy, iw, ih);
        DataF navlo_cropped = dataf_crop(&navlo, ix, iy, iw, ih);
        dataf_destroy(&navla); dataf_destroy(&navlo);
        navla = navla_cropped; navlo = navlo_cropped;
    }
    
    // --- Escritura de archivo ---
    if (is_geotiff) {
        DataNC meta_out = {0};
        
        if (do_reprojection) {
            // MODO: Geográficas (Lat/Lon)
            meta_out.proj_code = PROJ_LATLON;
            
            // Construir GeoTransform [TL_X, W_Px, 0, TL_Y, 0, H_Px]
            meta_out.geotransform[0] = final_lon_min;
            meta_out.geotransform[1] = (final_lon_max - final_lon_min) / (double)imout.width;
            meta_out.geotransform[2] = 0.0;
            meta_out.geotransform[3] = final_lat_max;
            meta_out.geotransform[4] = 0.0;
            // Altura de pixel suele ser negativa (Norte -> Sur)
            meta_out.geotransform[5] = (final_lat_min - final_lat_max) / (double)imout.height; 

            // Pasamos offset 0,0 porque la imagen y el geotransform ya están alineados
            if (is_pseudocolor && color_array) {
                write_geotiff_indexed(outfn, &imout, color_array, &meta_out, 0, 0);
            } else {
                write_geotiff_gray(outfn, &imout, &meta_out, 0, 0);
            }
        } else {
            // MODO: Nativo (Geoestacionario)
            // Copiamos la info del NetCDF original
            meta_out = c01; 
            
            // Pasamos el offset del recorte para que GDAL ajuste el origen
            if (is_pseudocolor && color_array) {
                write_geotiff_indexed(outfn, &imout, color_array, &meta_out, crop_x_start, crop_y_start);
            } else {
                write_geotiff_gray(outfn, &imout, &meta_out, crop_x_start, crop_y_start);
            }
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
