#define _GNU_SOURCE
/*
 * RGB and day/night composite generation module.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "rgb.h"
#include "reader_nc.h"
#include "writer_png.h"
#include "writer_geotiff.h"
#include "image.h"
#include "logger.h"
#include "datanc.h"
#include "reprojection.h"
#include "filename_utils.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>


// --- Estructuras para manejo de canales (antes en main.c) ---
typedef struct {
    const char* name;
    char* filename;
} ChannelInfo;

typedef struct {
    ChannelInfo* channels;
    int count;
    char* id_signature;
} ChannelSet;

// --- Prototipos de funciones internas ---
ImageData create_truecolor_rgb(DataF B, DataF R, DataF NiR);
ImageData create_nocturnal_pseudocolor(DataNC datanc);

/**
 * @brief Normaliza un DataF a un buffer de 8 bits (0-255).
 *
 * @param data Puntero al DataF de entrada.
 * @param min_val El valor de los datos que se mapeará a 0.
 * @param max_val El valor de los datos que se mapeará a 255.
 * @return Un puntero a un nuevo buffer de `unsigned char`. El llamador es responsable de liberarlo.
 */
static unsigned char* normalize_to_u8(const DataF* data, float min_val, float max_val) {
    unsigned char* buffer = malloc(data->size * sizeof(unsigned char));
    if (!buffer) {
        LOG_FATAL("Falla de memoria al normalizar canal a 8-bit.");
        return NULL;
    }

    float range = max_val - min_val;
    if (range < 1e-9) range = 1.0f; // Evitar división por cero.

    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        float val = data->data_in[i];
        if (val == NonData) {
            buffer[i] = 0;
        } else {
            float normalized = (val - min_val) / range;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;
            buffer[i] = (unsigned char)(normalized * 255.0f);
        }
    }
    return buffer;
}

/**
 * @brief Crea una imagen RGB a partir de tres bandas DataF con escalado personalizado.
 *
 * @return Una nueva estructura ImageData.
 */
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max) {
    if (r_ch->size != g_ch->size || r_ch->size != b_ch->size) {
        LOG_ERROR("Las dimensiones de las bandas de entrada para RGB no coinciden.");
        return image_create(0, 0, 0);
    }

    ImageData imout = image_create(r_ch->width, r_ch->height, 3);
    if (imout.data == NULL) return imout;

    unsigned char* r_norm = normalize_to_u8(r_ch, r_min, r_max);
    unsigned char* g_norm = normalize_to_u8(g_ch, g_min, g_max);
    unsigned char* b_norm = normalize_to_u8(b_ch, b_min, b_max);

    size_t num_pixels = imout.width * imout.height;
    #pragma omp parallel for
    for (size_t i = 0; i < num_pixels; i++) {
        imout.data[i * 3]     = r_norm[i];
        imout.data[i * 3 + 1] = g_norm[i];
        imout.data[i * 3 + 2] = b_norm[i];
    }

    free(r_norm); free(g_norm); free(b_norm);
    return imout;
}

ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo, float *dnratio, float max_tmp);
ChannelSet* channelset_create(const char* channel_names[], int count);
void channelset_destroy(ChannelSet* set);

// --- Implementación de la función principal del módulo ---
int find_id_from_name(const char *name, char *id_buffer);
int find_channel_filenames(const char *dirnm, ChannelSet* channelset, bool is_l2_product);

// --- Implementación de funciones de ayuda para canales ---
ChannelSet* channelset_create(const char* channel_names[], int count) {
    ChannelSet* set = malloc(sizeof(ChannelSet));
    if (!set) return NULL;
    set->channels = malloc(sizeof(ChannelInfo) * count);
    if (!set->channels) { free(set); return NULL; }
    // Allocate enough space for the full s..._e... signature
    set->id_signature = malloc(40);
    if (!set->id_signature) { free(set->channels); free(set); return NULL; }
    set->count = count;
    for (int i = 0; i < count; i++) {
        set->channels[i].name = channel_names[i];
        set->channels[i].filename = NULL;
    }
    return set;
}

void channelset_destroy(ChannelSet* set) {
    if (!set) return;
    if (set->channels) {
        for (int i = 0; i < set->count; i++) {
            free(set->channels[i].filename);
        }
        free(set->channels);
    }
    free(set->id_signature);
    free(set);
}

/**
 * @brief Extrae una firma de tiempo única de un nombre de archivo GOES.
 *
 * Esta función busca el patrón "_s<timestamp>_e<timestamp>_" que es común
 * en los nombres de archivo GOES L1b y L2. Esta firma se usa para encontrar
 * los archivos de los otros canales correspondientes al mismo instante.
 *
 * @param name El nombre base del archivo de entrada.
 * @param id_buffer El buffer donde se almacenará la firma extraída.
 * @return 0 en éxito, -1 si el patrón no se encuentra.
 */
int find_id_from_name(const char *name, char *id_buffer) {
    const char* s_pos = strstr(name, "_s");
    if (!s_pos) return -1;

    const char* e_pos = strstr(s_pos, "_e");
    if (!e_pos) return -1;

    // La firma de tiempo única y consistente entre canales es la que va desde
    // el inicio de '_s' hasta justo antes de '_e'.
    size_t length = (size_t)(e_pos - s_pos);
    strncpy(id_buffer, s_pos + 1, length - 1); // Copia solo el contenido, sin los '_'
    id_buffer[length] = '\0';
    return 0;
}

char *concat(const char *s1, const char *s2) {
    char *result = malloc(strlen(s1) + strlen(s2) + 2);
    if (!result) return NULL;
    sprintf(result, "%s/%s", s1, s2);
    return result;
}

int find_channel_filenames(const char *dirnm, ChannelSet* channelset, bool is_l2_product) {
    DIR *d = opendir(dirnm);
    if (!d) return -1;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        // Condición base: debe coincidir la firma de tiempo y ser un archivo .nc
        if (strstr(dir->d_name, channelset->id_signature) && strstr(dir->d_name, ".nc")) {
            
            // Si es un producto L2, debe contener "CMIP" para evitar otros productos L2.
            // Si no es L2, esta condición se ignora.
            if (is_l2_product && !strstr(dir->d_name, "CMIP")) {
                continue;
            }

            for (int i = 0; i < channelset->count; i++) {
                if (strstr(dir->d_name, channelset->channels[i].name)) {
                    free(channelset->channels[i].filename);
                    channelset->channels[i].filename = concat(dirnm, dir->d_name);
                    LOG_DEBUG("Encontrado archivo para canal %s: %s", channelset->channels[i].name, channelset->channels[i].filename);
                    break;
                }
            }
        }
    }
    closedir(d);
    return 0;
}

int run_rgb(ArgParser* parser) {
    LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
    logger_init(log_level);
    LOG_DEBUG("Modo verboso activado para el comando 'rgb'.");

    const char* input_file;
    if (ap_has_args(parser)) {
        input_file = ap_get_arg_at_index(parser, 0);
    } else {
        LOG_ERROR("El comando 'rgb' requiere un archivo NetCDF de entrada como referencia.");
        return -1;
    }
    
    // --- Opciones de procesamiento ---
    const bool do_reprojection = ap_found(parser, "geographics");
    const float gamma = ap_get_dbl_value(parser, "gamma");
    const char* mode = ap_get_str_value(parser, "mode");
    const bool apply_histogram = ap_found(parser, "histo");
    const bool use_alpha = ap_found(parser, "alpha");
    const bool force_geotiff = ap_found(parser, "geotiff");
    const bool apply_rayleigh = ap_found(parser, "rayleigh");
    const int scale = ap_get_int_value(parser, "scale"); // No implementado aún

    const char *basenm = basename((char*)input_file);
    char *dirnm_dup = strdup(input_file);
    const char *dirnm = dirname(dirnm_dup);

    bool is_l2_product = (strstr(basenm, "CMIP") != NULL);

    ChannelSet* channels = NULL;
    if (strcmp(mode, "composite") == 0 || strcmp(mode, "truecolor") == 0) {
        const char* required[] = {"C01", "C02", "C03", "C13"};
        channels = channelset_create(required, 4);
    } else if (strcmp(mode, "night") == 0) {
        const char* required[] = {"C13"};
        channels = channelset_create(required, 1);
    } else if (strcmp(mode, "ash") == 0) {
        const char* required[] = {"C11", "C13", "C14", "C15"};
        channels = channelset_create(required, 4);
    } else if (strcmp(mode, "airmass") == 0) {
        const char* required[] = {"C08", "C10", "C12", "C13"};
        channels = channelset_create(required, 4);
    } else if (strcmp(mode, "so2") == 0) {
        const char* required[] = {"C09", "C10", "C11", "C13"};
        channels = channelset_create(required, 4);
    } else {
        LOG_ERROR("Modo '%s' no reconocido.", mode);
        free(dirnm_dup);
        return -1;
    }

    if (!channels) { LOG_FATAL("No se pudo crear el conjunto de canales."); free(dirnm_dup); return -1; }

    if (find_id_from_name(basenm, channels->id_signature) != 0) {
        LOG_ERROR("No se pudo extraer el ID del nombre de archivo: %s", basenm);
        channelset_destroy(channels); free(dirnm_dup); return -1;
    }

    if (find_channel_filenames(dirnm, channels, is_l2_product) != 0) {
        LOG_ERROR("No se pudo acceder al directorio %s", dirnm);
        channelset_destroy(channels); free(dirnm_dup); return -1;
    }
    free(dirnm_dup);

    ChannelInfo* c_info[17] = {NULL};
    for (int i = 0; i < channels->count; i++) {
        if (channels->channels[i].filename == NULL) {
            LOG_ERROR("Archivo faltante para el canal %s con ID %s", channels->channels[i].name, channels->id_signature);
            channelset_destroy(channels); return -1;
        }
        int channel_num = atoi(channels->channels[i].name + 1);
        if (channel_num > 0 && channel_num <= 16) c_info[channel_num] = &channels->channels[i];
    }

    DataNC c[17];
    DataF aux, navlo, navla;
    const char* var_name = is_l2_product ? "CMI" : "Rad";

    for (int i = 1; i <= 16; i++) { if (c_info[i]) load_nc_sf(c_info[i]->filename, var_name, &c[i]); }

    int ref_ch_idx = 0;
    for (int i = 16; i > 0; i--) { if (c_info[i]) { ref_ch_idx = i; break; } }
    if (ref_ch_idx == 0) { LOG_ERROR("No se encontró ningún canal de referencia."); channelset_destroy(channels); return -1; }

    if (c_info[1]) { aux = downsample_boxfilter(c[1].fdata, 2); dataf_destroy(&c[1].fdata); c[1].fdata = aux; }
    if (c_info[2]) { aux = downsample_boxfilter(c[2].fdata, 4); dataf_destroy(&c[2].fdata); c[2].fdata = aux; }
    if (c_info[3]) { aux = downsample_boxfilter(c[3].fdata, 2); dataf_destroy(&c[3].fdata); c[3].fdata = aux; }
    
    compute_navigation_nc(c_info[ref_ch_idx]->filename, &navla, &navlo);

    unsigned int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    bool has_clip = ap_found(parser, "clip") && (ap_count(parser, "clip") == 4);
    float clip_coords[4] = {0};  // Declarar fuera para usarlo en write_geotiff_rgb
    if (has_clip) {
        for(int i=0; i<4; i++) clip_coords[i] = atof(ap_get_str_value_at_index(parser, "clip", i));
    }
    
    // Guardar índices del crop para GeoTIFF geoestacionario (caso sin reproyección)
    unsigned crop_x_start = 0, crop_y_start = 0;
    
    if (has_clip && do_reprojection) {
        LOG_INFO("Aplicando recorte PRE-reproyección: lon[%.3f, %.3f], lat[%.3f, %.3f]", clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);

        int ix, iy, iw, ih, vs;
        vs = reprojection_find_bounding_box(&navla, &navlo, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3], &ix, &iy, &iw, &ih);
        
        if (vs < 4) {
            LOG_ERROR("Dominio de clip fuera del disco visible (solo %d muestras válidas).", vs);
        } else {
            clip_x = (unsigned int)ix; clip_y = (unsigned int)iy; clip_w = (unsigned int)iw; clip_h = (unsigned int)ih;
            crop_x_start = clip_x; crop_y_start = clip_y;  // Guardar para GeoTIFF
            LOG_INFO("Recortando datos pre-reproyección: start[%u, %u], size[%u, %u]", clip_x, clip_y, clip_w, clip_h);

            for (int i = 1; i <= 16; i++) {
                if (c_info[i]) {
                    DataF cropped = dataf_crop(&c[i].fdata, clip_x, clip_y, clip_w, clip_h);
                    dataf_destroy(&c[i].fdata); c[i].fdata = cropped;
                }
            }
            DataF navla_c = dataf_crop(&navla, clip_x, clip_y, clip_w, clip_h);
            DataF navlo_c = dataf_crop(&navlo, clip_x, clip_y, clip_w, clip_h);
            dataf_destroy(&navla); dataf_destroy(&navlo); navla = navla_c; navlo = navlo_c;
        }
    }

    if (do_reprojection) {
        LOG_INFO("Iniciando reproyección...");
        float lon_min, lon_max, lat_min, lat_max;
        const char* nav_ref = c_info[ref_ch_idx]->filename;
        bool first = true;
        for (int i = 1; i <= 16; i++) {
            if (c_info[i]) {
                DataF repro = has_clip 
                    ? reproject_to_geographics_with_nav(&c[i].fdata, &navla, &navlo, c[i].native_resolution_km, first ? &lon_min : NULL, first ? &lon_max : NULL, first ? &lat_min : NULL, first ? &lat_max : NULL)
                    : reproject_to_geographics(&c[i].fdata, nav_ref, c[i].native_resolution_km, first ? &lon_min : NULL, first ? &lon_max : NULL, first ? &lat_min : NULL, first ? &lat_max : NULL);
                dataf_destroy(&c[i].fdata); c[i].fdata = repro;
                first = false;
            }
        }

        dataf_destroy(&navla); dataf_destroy(&navlo);
        size_t final_w = c[ref_ch_idx].fdata.width, final_h = c[ref_ch_idx].fdata.height;
        
        create_navigation_from_reprojected_bounds(&navla, &navlo, final_w, final_h, lon_min, lon_max, lat_min, lat_max);
        
        if (has_clip) {
            LOG_INFO("Aplicando recorte POST-reproyección: lon[%.3f, %.3f], lat[%.3f, %.3f]", clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);

            int ix_s = (int)(((clip_coords[0] - lon_min) / (lon_max-lon_min)) * final_w);
            int iy_s = (int)(((lat_max - clip_coords[1]) / (lat_max-lat_min)) * final_h);
            int ix_e = (int)(((clip_coords[2] - lon_min) / (lon_max-lon_min)) * final_w);
            int iy_e = (int)(((lat_max - clip_coords[3]) / (lat_max-lat_min)) * final_h);
            
            ix_s = (ix_s < 0) ? 0 : ix_s; iy_s = (iy_s < 0) ? 0 : iy_s;
            unsigned int crop_w = (unsigned int)((ix_e > ix_s) ? (ix_e - ix_s) : 0);
            unsigned int crop_h = (unsigned int)((iy_e > iy_s) ? (iy_e - iy_s) : 0);
            
            for (int i=1; i<=16; i++) if (c_info[i]) {
                DataF cropped = dataf_crop(&c[i].fdata, ix_s, iy_s, crop_w, crop_h);
                dataf_destroy(&c[i].fdata); c[i].fdata = cropped;
            }
            dataf_destroy(&navla); dataf_destroy(&navlo);
            create_navigation_from_reprojected_bounds(&navla, &navlo, crop_w, crop_h, clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);
        }
    }

    char* out_filename_generated = NULL;
    const char* out_filename;
    if (ap_found(parser, "out")) {
        out_filename = ap_get_str_value(parser, "out");
    } else {
        const char* ts_ref = c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : input_file;
        const char* ext = force_geotiff ? ".tif" : ".png";
        char* base_fn = generate_default_output_filename(ts_ref, mode, ext);
        if (do_reprojection && base_fn) {
            char* dot = strrchr(base_fn, '.');
            if (dot) {
                size_t pre_len = dot - base_fn;
                size_t total = strlen(base_fn) + 5;
                out_filename_generated = (char*)malloc(total);
                if (out_filename_generated) {
                    strncpy(out_filename_generated, base_fn, pre_len);
                    out_filename_generated[pre_len] = '\0';
                    strcat(out_filename_generated, "_geo");
                    strcat(out_filename_generated, dot);
                    free(base_fn);
                } else out_filename_generated = base_fn;
            } else out_filename_generated = base_fn;
        } else out_filename_generated = base_fn;
        out_filename = out_filename_generated;
    }

    ImageData final_image = {0};
    DataF r_ch = {0}, g_ch = {0}, b_ch = {0};
    
    if (strcmp(mode, "truecolor") == 0) {
        LOG_INFO("Generando imagen en modo 'truecolor'...");
        final_image = apply_rayleigh ? create_truecolor_rgb_rayleigh(c[1].fdata, c[2].fdata, c[3].fdata, c_info[1]->filename, true)
                                     : create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
    } else if (strcmp(mode, "night") == 0) {
        LOG_INFO("Generando imagen en modo 'night'...");
        final_image = create_nocturnal_pseudocolor(c[13]);
    } else if (strcmp(mode, "ash") == 0) {
        LOG_INFO("Generando imagen en modo 'ash'...");
        r_ch = dataf_op_dataf(&c[15].fdata, &c[13].fdata, OP_SUB);
        g_ch = dataf_op_dataf(&c[14].fdata, &c[11].fdata, OP_SUB);
        final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -6.7f, 2.6f, -6.0f, 6.3f, 243.6f, 302.4f);
    } else if (strcmp(mode, "airmass") == 0) {
        LOG_INFO("Generando imagen en modo 'airmass'...");
        r_ch = dataf_op_dataf(&c[8].fdata, &c[10].fdata, OP_SUB);
        g_ch = dataf_op_dataf(&c[12].fdata, &c[13].fdata, OP_SUB);
        b_ch = dataf_op_scalar(&c[8].fdata, 273.15f, OP_SUB, true);
        final_image = create_multiband_rgb(&r_ch, &g_ch, &b_ch, -26.2f, 0.6f, -43.2f, 6.7f, 29.25f, 64.65f);
    } else if (strcmp(mode, "so2") == 0) {
        LOG_INFO("Generando imagen en modo 'so2'...");
        r_ch = dataf_op_dataf(&c[9].fdata, &c[10].fdata, OP_SUB);
        g_ch = dataf_op_dataf(&c[13].fdata, &c[11].fdata, OP_SUB);
        final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -4.0f, 2.0f, -4.0f, 5.0f, 233.0f, 300.0f);
    } else { // composite
        LOG_INFO("Generando imagen en modo 'composite'...");
        ImageData diurna = apply_rayleigh ? create_truecolor_rgb_rayleigh(c[1].fdata, c[2].fdata, c[3].fdata, c_info[1]->filename, true)
                                          : create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
        if (apply_histogram) image_apply_histogram(diurna);
        ImageData nocturna = create_nocturnal_pseudocolor(c[13]);
        float dnratio;
        ImageData mask = create_daynight_mask(c[13], navla, navlo, &dnratio, 263.15);
        LOG_INFO("Ratio día/noche: %.2f%%", dnratio);
        final_image = blend_images(nocturna, diurna, mask);
        image_destroy(&diurna); image_destroy(&nocturna); image_destroy(&mask);
    }

    if (gamma != 1.0) image_apply_gamma(final_image, gamma);
    if (strcmp(mode, "truecolor") != 0 && apply_histogram) image_apply_histogram(final_image);

    if (has_clip && !do_reprojection) {
        LOG_INFO("Aplicando recorte POST-procesamiento: lon[%.3f, %.3f], lat[%.3f, %.3f]", clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);
        int ix, iy, iw, ih, vs;
        vs = reprojection_find_bounding_box(&navla, &navlo, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3], &ix, &iy, &iw, &ih);
        if (vs < 4) {
            LOG_WARN("Dominio de clip fuera del disco. Ignorando recorte.");
        } else {
            ImageData cropped = image_crop(&final_image, ix, iy, iw, ih);
            if (cropped.data) { image_destroy(&final_image); final_image = cropped; }
        }
    }

    bool is_geotiff = force_geotiff || (out_filename && (strstr(out_filename, ".tif") || strstr(out_filename, ".tiff")));
    if (is_geotiff) {
        const char* nc_ref = c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : NULL;
        if (nc_ref) {
            write_geotiff_rgb(out_filename, &final_image, &navla, &navlo, nc_ref, do_reprojection, crop_x_start, crop_y_start, 0, 0);
        } else {
            LOG_ERROR("No se puede escribir GeoTIFF: falta archivo NetCDF de referencia.");
        }
    } else {
        write_image_png(out_filename, &final_image);
    }
    LOG_INFO("Imagen guardada en: %s", out_filename);
    
    if (scale != 1) LOG_WARN("La opción --scale aún no está implementada para el comando rgb.");
    if (use_alpha) LOG_WARN("La opción --alpha aún no está implementada para el comando rgb.");

    dataf_destroy(&r_ch); dataf_destroy(&g_ch); dataf_destroy(&b_ch);
    if (out_filename_generated) free(out_filename_generated);
    image_destroy(&final_image);
    for (int i = 1; i <= 16; i++) { if (c_info[i]) datanc_destroy(&c[i]); }
    dataf_destroy(&navla); dataf_destroy(&navlo);
    channelset_destroy(channels);
    return 0;
}