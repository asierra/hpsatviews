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

bool strinstr(const char *main_str, const char *sub) {
    return (main_str && sub && strstr(main_str, sub));
}

int run_processing(ArgParser *parser, bool is_pseudocolor) {
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

    DataNC c01;
    char *varname = "Rad"; // Default
    if (strinstr(fnc01, "LST")) varname = "LST";
    else if (strinstr(fnc01, "ACTP")) varname = "Phase";
    else if (strinstr(fnc01, "CTP")) varname = "PRES";

    if (load_nc_sf(fnc01, varname, &c01) != 0) {
        LOG_ERROR("No se pudo cargar el archivo: %s", fnc01);
        free_cpt_data(cptdata);
        color_array_destroy(color_array);
        return -1;
    }

    if (do_reprojection) {
        DataF reprojected_data = reproject_to_geographics(&c01.base, fnc01);
        if (reprojected_data.data_in) {
            dataf_destroy(&c01.base);
            c01.base = reprojected_data;
        }
    }

    if (scale < 0) {
        DataF aux = downsample_boxfilter(c01.base, -scale);
        dataf_destroy(&c01.base);
        c01.base = aux;
    } else if (scale > 1) {
        DataF aux = upsample_bilinear(c01.base, scale);
        dataf_destroy(&c01.base);
        c01.base = aux;
    }

    ImageData imout = create_single_gray(c01.base, invert_values, use_alpha, cptdata);
    if (gamma != 1.0) image_apply_gamma(imout, gamma);
    if (apply_histogram) image_apply_histogram(imout);

    if (is_pseudocolor && color_array) {
        write_image_png_palette(outfn, &imout, color_array);
    } else {
        write_image_png(outfn, &imout);
    }

    LOG_INFO("Imagen guardada en: %s", outfn);

    free_cpt_data(cptdata);
    dataf_destroy(&c01.base);
    image_destroy(&imout);
    color_array_destroy(color_array);

    return 0;
}