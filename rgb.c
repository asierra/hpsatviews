/*
 * RGB and day/night composite generation module.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "rgb.h"
#include "reader_nc.h"
#include "writer_png.h"
#include "image.h"
#include "logger.h"
#include "reprojection.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int run_rgb(ArgParser* parser) { // This is the correct, single definition
    const char* input_file;
    bool do_reprojection = ap_found(parser, "geographics");
    float gamma = ap_get_dbl_value(parser, "gamma");
    const char* mode = ap_get_str_value(parser, "mode");

    if (ap_has_args(parser)) {
        input_file = ap_get_arg_at_index(parser, 0);
    } else {
        LOG_ERROR("El comando 'rgb' requiere un archivo NetCDF de entrada como referencia.");
        return -1;
    }

    const char *basenm = basename((char*)input_file);
    const char *dirnm = dirname((char*)input_file);

    // Determinar si el archivo de referencia es un producto L2 (CMIP)
    bool is_l2_product = (strstr(basenm, "CMIP") != NULL);

    // Determinar los canales requeridos según el modo
    const char* required_channels_all[] = {"C01", "C02", "C03", "C13"};
    const char* required_channels_day[] = {"C01", "C02", "C03", "C13"}; // C13 para referencia
    const char* required_channels_night[] = {"C13"};
    
    ChannelSet* channels = NULL;
    if (strcmp(mode, "night") == 0) {
        channels = channelset_create(required_channels_night, 1);
    } else { // "composite" y "truecolor" necesitan los 4 (C13 como referencia)
        channels = channelset_create(required_channels_all, 4);
    }
    if (!channels) {
        LOG_FATAL("Error: No se pudo crear el conjunto de canales.");
        return -1;
    }

    if (find_id_from_name(basenm, channels->id_signature) != 0) {
        LOG_ERROR("No se pudo extraer el ID del nombre de archivo: %s", basenm);
        channelset_destroy(channels);
        return -1;
    }

    if (find_channel_filenames(dirnm, channels, is_l2_product) != 0) {
        LOG_ERROR("No se pudo acceder al directorio %s", dirnm);
        channelset_destroy(channels);
        return -1;
    }

    ChannelInfo* c01_info = NULL, *c02_info = NULL, *c03_info = NULL, *c13_info = NULL;
    for (int i = 0; i < channels->count; i++) {
        if (channels->channels[i].filename == NULL) {
            LOG_ERROR("Archivo faltante para el canal %s con ID %s en el directorio %s", channels->channels[i].name, channels->id_signature, dirnm);
            channelset_destroy(channels);
            return -1;
        }
        if (strcmp(channels->channels[i].name, "C01") == 0) c01_info = &channels->channels[i];
        if (strcmp(channels->channels[i].name, "C02") == 0) c02_info = &channels->channels[i];
        if (strcmp(channels->channels[i].name, "C03") == 0) c03_info = &channels->channels[i];
        if (strcmp(channels->channels[i].name, "C13") == 0) c13_info = &channels->channels[i];
    }

    DataNC c01, c02, c03, c13;
    DataF aux, navlo, navla;
    const char* var_name = is_l2_product ? "CMI" : "Rad";

    // Cargar solo los canales necesarios
    if (c01_info) load_nc_sf(c01_info->filename, var_name, &c01);
    if (c02_info) load_nc_sf(c02_info->filename, var_name, &c02);
    if (c03_info) load_nc_sf(c03_info->filename, var_name, &c03);
    load_nc_sf(c13_info->filename, var_name, &c13);

    // Iguala los tamaños a la resolución mínima (la de C13)
    if (c01_info) { aux = downsample_boxfilter(c01.base, 2); dataf_destroy(&c01.base); c01.base = aux; }
    if (c02_info) { aux = downsample_boxfilter(c02.base, 4); dataf_destroy(&c02.base); c02.base = aux; }
    if (c03_info) { aux = downsample_boxfilter(c03.base, 2); dataf_destroy(&c03.base); c03.base = aux; }
    compute_navigation_nc(c13_info->filename, &navla, &navlo);
    
    if (do_reprojection) {
        LOG_INFO("Iniciando reproyección para todos los canales...");
        const char* nav_ref = c13_info->filename;
        float lon_min, lon_max, lat_min, lat_max;

        // Reproyectar los datos de imagen. La primera llamada obtiene los límites geográficos.
        if (c01_info) { DataF repro_c01 = reproject_to_geographics(&c01.base, nav_ref, &lon_min, &lon_max, &lat_min, &lat_max); dataf_destroy(&c01.base); c01.base = repro_c01; }
        if (c02_info) { DataF repro_c02 = reproject_to_geographics(&c02.base, nav_ref, NULL, NULL, NULL, NULL); dataf_destroy(&c02.base); c02.base = repro_c02; }
        if (c03_info) { DataF repro_c03 = reproject_to_geographics(&c03.base, nav_ref, NULL, NULL, NULL, NULL); dataf_destroy(&c03.base); c03.base = repro_c03; }
        DataF repro_c13 = reproject_to_geographics(&c13.base, nav_ref, NULL, NULL, NULL, NULL);

        // Si no se reproyectó c01, necesitamos obtener los límites desde c13
        if (!c01_info) {
            repro_c13 = reproject_to_geographics(&c13.base, nav_ref, &lon_min, &lon_max, &lat_min, &lat_max);
        }
        dataf_destroy(&c13.base); c13.base = repro_c13;

        // Liberar la navegación original (geostacionaria)
        dataf_destroy(&navla);
        dataf_destroy(&navlo);

        // Crear la nueva navegación directamente sobre la malla geográfica, sin reproyectar.
        size_t final_width = c01_info ? c01.base.width : c13.base.width;
        size_t final_height = c01_info ? c01.base.height : c13.base.height;
        LOG_INFO("Creando navegación para la malla geográfica final...");
        create_navigation_from_reprojected_bounds(&navla, &navlo, final_width, final_height, lon_min, lon_max, lat_min, lat_max);

        LOG_INFO("Reproyección completada.");
    }

    const char* out_filename = ap_get_str_value(parser, "out");

    if (strcmp(mode, "truecolor") == 0) {
        LOG_INFO("Generando imagen en modo 'truecolor'...");
        ImageData diurna = create_truecolor_rgb(c01.base, c02.base, c03.base);
        image_apply_histogram(diurna);
        if (gamma != 1.0) image_apply_gamma(diurna, gamma);
        write_image_png(out_filename, &diurna);
        image_destroy(&diurna);

    } else if (strcmp(mode, "night") == 0) {
        LOG_INFO("Generando imagen en modo 'night'...");
        ImageData nocturna = create_nocturnal_pseudocolor(c13);
        if (gamma != 1.0) image_apply_gamma(nocturna, gamma);
        write_image_png(out_filename, &nocturna);
        image_destroy(&nocturna);

    } else { // Modo "composite" (default)
        LOG_INFO("Generando imagen en modo 'composite'...");
        ImageData diurna = create_truecolor_rgb(c01.base, c02.base, c03.base);
        image_apply_histogram(diurna);
        ImageData nocturna = create_nocturnal_pseudocolor(c13);

        float dnratio;
        ImageData mask = create_daynight_mask(c13, navla, navlo, &dnratio, 263.15);
        LOG_INFO("Ratio día/noche: %.2f%%", dnratio);

        ImageData blend = blend_images(nocturna, diurna, mask);
        if (gamma != 1.0) image_apply_gamma(blend, gamma);
        write_image_png(out_filename, &blend);
        image_destroy(&blend);
        image_destroy(&diurna);
        image_destroy(&nocturna);
        image_destroy(&mask);
    }
    LOG_INFO("Imagen RGB guardada en: %s", out_filename);

    dataf_destroy(&c01.base); dataf_destroy(&c02.base); dataf_destroy(&c03.base); dataf_destroy(&c13.base);
    dataf_destroy(&navla); dataf_destroy(&navlo);
    channelset_destroy(channels);
    return 0;
}