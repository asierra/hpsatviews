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
#include "reprojection.h"
#include "image.h"
#include "datanc.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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

    char *fnc01, *outfn;
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

    outfn = ap_get_str_value(parser, "out");
    invert_values = ap_found(parser, "invert");
    apply_histogram = ap_found(parser, "histo");
    use_alpha = ap_found(parser, "alpha");
    do_reprojection = ap_found(parser, "geographics");
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

    ImageData imout;

    if (c01.is_float) {
        // --- RUTA PARA DATOS FLOAT ---
        if (do_reprojection) {
            DataF reprojected_data = reproject_to_geographics(&c01.fdata, fnc01, NULL, NULL, NULL, NULL);
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

    // --- LÓGICA DE RECORTE ---
    if (ap_found(parser, "clip")) {
        DataF navla, navlo;
        bool nav_loaded = false;
        if (ap_count(parser, "clip") == 4) {
            // Cargar navegación solo si es necesario
            if (compute_navigation_nc(fnc01, &navla, &navlo) == 0) {
                nav_loaded = true;
            } else {
                LOG_WARN("No se pudo cargar la navegación para el recorte. Ignorando --clip.");
            }
        }

        if (nav_loaded) {
            float clip_lon_min = atof(ap_get_str_value_at_index(parser, "clip", 0));
            float clip_lat_max = atof(ap_get_str_value_at_index(parser, "clip", 1));
            float clip_lon_max = atof(ap_get_str_value_at_index(parser, "clip", 2));
            float clip_lat_min = atof(ap_get_str_value_at_index(parser, "clip", 3));

            LOG_INFO("Aplicando recorte geográfico: lon[%.3f, %.3f], lat[%.3f, %.3f]", clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);

            int ix_start, iy_start, ix_end, iy_end;
            if (do_reprojection) {
                // Caso reproyectado: usar interpolación lineal
                float lon_range = navlo.fmax - navlo.fmin;
                float lat_range = navla.fmax - navla.fmin;
                ix_start = (int)(((clip_lon_min - navlo.fmin) / lon_range) * imout.width);
                iy_start = (int)(((navla.fmax - clip_lat_max) / lat_range) * imout.height);
                ix_end = (int)(((clip_lon_max - navlo.fmin) / lon_range) * imout.width);
                iy_end = (int)(((navla.fmax - clip_lat_min) / lat_range) * imout.height);
            } else {
                // Caso original: usar búsqueda de píxeles
                reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_min, &ix_start, &iy_start);
                reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_max, &ix_end, &iy_end);
            }

            unsigned int crop_width = (ix_end > ix_start) ? (ix_end - ix_start) : 0;
            unsigned int crop_height = (iy_end > iy_start) ? (iy_end - iy_start) : 0;
            LOG_INFO("Dimensiones de recorte en píxeles: start[%u, %u], size[%u, %u]", ix_start, iy_start, crop_width, crop_height);

            ImageData cropped_image = image_crop(&imout, ix_start, iy_start, crop_width, crop_height);
            if (cropped_image.data) { image_destroy(&imout); imout = cropped_image; }
        }
        dataf_destroy(&navla); dataf_destroy(&navlo);
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

    return 0;
}