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
int run_rgb(ArgParser* parser) {
    const char* input_file;
    bool do_reprojection = ap_found(parser, "geographics");
    float gamma = ap_get_dbl_value(parser, "gamma");

    if (ap_has_args(parser)) {
        input_file = ap_get_arg_at_index(parser, 0);
    } else {
        LOG_ERROR("El comando 'rgb' requiere un archivo NetCDF de entrada como referencia.");
        return -1;
    }

    const char *basenm = basename((char*)input_file);
    const char *dirnm = dirname((char*)input_file);

    const char* required_channels[] = {"C01", "C02", "C03", "C13"};
    ChannelSet* channels = channelset_create(required_channels, 4);
    if (!channels) {
        LOG_FATAL("Error: No se pudo crear el conjunto de canales.");
        return -1;
    }

    int find_id_from_name(const char *name, char *id_buffer);
    int find_channel_filenames(const char *dirnm, ChannelSet* channelset);

    strcpy(channels->id_signature, "sAAAAJJJHHmm");
    if (find_id_from_name(basenm, channels->id_signature) != 0) {
        LOG_ERROR("No se pudo extraer el ID del nombre de archivo: %s", basenm);
        channelset_destroy(channels);
        return -1;
    }

    if (find_channel_filenames(dirnm, channels) != 0) {
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
    load_nc_sf(c01_info->filename, "Rad", &c01);
    load_nc_sf(c02_info->filename, "Rad", &c02);
    load_nc_sf(c03_info->filename, "Rad", &c03);
    load_nc_sf(c13_info->filename, "Rad", &c13);

    // Iguala los tamaños a la resolución mínima (la de C13)
    aux = downsample_boxfilter(c01.base, 2); dataf_destroy(&c01.base); c01.base = aux;
    aux = downsample_boxfilter(c02.base, 4); dataf_destroy(&c02.base); c02.base = aux;
    aux = downsample_boxfilter(c03.base, 2); dataf_destroy(&c03.base); c03.base = aux;
    compute_navigation_nc(c13_info->filename, &navla, &navlo);
    
    if (do_reprojection) {
        LOG_INFO("Iniciando reproyección para todos los canales...");
        const char* nav_ref = c13_info->filename;
        float lon_min, lon_max, lat_min, lat_max;

        // Reproyectar los datos de imagen. La primera llamada obtiene los límites geográficos.
        DataF repro_c01 = reproject_to_geographics(&c01.base, nav_ref, &lon_min, &lon_max, &lat_min, &lat_max);
        DataF repro_c02 = reproject_to_geographics(&c02.base, nav_ref, NULL, NULL, NULL, NULL);
        DataF repro_c03 = reproject_to_geographics(&c03.base, nav_ref, NULL, NULL, NULL, NULL);
        DataF repro_c13 = reproject_to_geographics(&c13.base, nav_ref, NULL, NULL, NULL, NULL);

        // Liberar las mallas originales y asignar las reproyectadas
        dataf_destroy(&c01.base); c01.base = repro_c01;
        dataf_destroy(&c02.base); c02.base = repro_c02;
        dataf_destroy(&c03.base); c03.base = repro_c03;
        dataf_destroy(&c13.base); c13.base = repro_c13;

        // Liberar la navegación original (geostacionaria)
        dataf_destroy(&navla);
        dataf_destroy(&navlo);

        // Crear la nueva navegación directamente sobre la malla geográfica, sin reproyectar.
        LOG_INFO("Creando navegación para la malla geográfica final...");
        create_navigation_from_reprojected_bounds(&navla, &navlo, c01.base.width, c01.base.height, lon_min, lon_max, lat_min, lat_max);

        LOG_INFO("Reproyección completada.");
    }

    ImageData diurna = create_truecolor_rgb(c01.base, c02.base, c03.base);
    image_apply_histogram(diurna);
    ImageData nocturna = create_nocturnal_pseudocolor(c13);

    float dnratio;
    ImageData mask = create_daynight_mask(c13, navla, navlo, &dnratio, 263.15);
    LOG_INFO("Ratio día/noche: %.2f%%", dnratio);

    const char* out_filename = ap_get_str_value(parser, "out");
    if (dnratio < 15.0) { // Casi todo de noche
        if (gamma != 1.0) image_apply_gamma(nocturna, gamma);
        write_image_png(out_filename, &nocturna);
    } else {
        ImageData blend = blend_images(nocturna, diurna, mask);
        if (gamma != 1.0) image_apply_gamma(blend, gamma);
        write_image_png(out_filename, &blend);
        image_destroy(&blend);
    }
    LOG_INFO("Imagen RGB guardada en: %s", out_filename);

    dataf_destroy(&c01.base); dataf_destroy(&c02.base); dataf_destroy(&c03.base); dataf_destroy(&c13.base);
    dataf_destroy(&navla); dataf_destroy(&navlo);
    image_destroy(&diurna); image_destroy(&nocturna); image_destroy(&mask);
    channelset_destroy(channels);
    return 0;
}

// --- Implementación de funciones de ayuda para canales ---
ChannelSet* channelset_create(const char* channel_names[], int count) {
    ChannelSet* set = malloc(sizeof(ChannelSet));
    if (!set) return NULL;
    set->channels = malloc(sizeof(ChannelInfo) * count);
    if (!set->channels) { free(set); return NULL; }
    set->id_signature = malloc(13);
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

int find_id_from_name(const char *name, char *id_buffer) {
    const char* s_pos = strchr(name, 's');
    if (!s_pos || strlen(s_pos) < 12) return -1;
    strncpy(id_buffer, s_pos, 12);
    id_buffer[12] = '\0';
    return 0;
}

char *concat(const char *s1, const char *s2) {
    char *result = malloc(strlen(s1) + strlen(s2) + 2);
    if (!result) return NULL;
    sprintf(result, "%s/%s", s1, s2);
    return result;
}

int find_channel_filenames(const char *dirnm, ChannelSet* channelset) {
    DIR *d = opendir(dirnm);
    if (!d) return -1;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strstr(dir->d_name, channelset->id_signature) && strstr(dir->d_name, ".nc")) {
            for (int i = 0; i < channelset->count; i++) {
                if (strstr(dir->d_name, channelset->channels[i].name)) {
                    free(channelset->channels[i].filename);
                    channelset->channels[i].filename = concat(dirnm, dir->d_name);
                    break;
                }
            }
        }
    }
    closedir(d);
    return 0;
}