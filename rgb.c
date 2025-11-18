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
#include "datanc.h"
#include "reprojection.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


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

/**
 * @brief Calcula el bounding box que contiene las 4 esquinas de un área de recorte.
 * 
 * @param ix_tl, iy_tl Coordenadas de la esquina superior izquierda
 * @param ix_tr, iy_tr Coordenadas de la esquina superior derecha
 * @param ix_bl, iy_bl Coordenadas de la esquina inferior izquierda
 * @param ix_br, iy_br Coordenadas de la esquina inferior derecha
 * @param x_start, y_start Salida: coordenadas de inicio del bounding box
 * @param width, height Salida: dimensiones del bounding box
 */
static void calculate_bounding_box(int ix_tl, int iy_tl, int ix_tr, int iy_tr,
                                   int ix_bl, int iy_bl, int ix_br, int iy_br,
                                   unsigned int* x_start, unsigned int* y_start,
                                   unsigned int* width, unsigned int* height) {
    // Encontrar min/max de x
    int min_ix = ix_tl;
    if (ix_tr < min_ix) min_ix = ix_tr;
    if (ix_bl < min_ix) min_ix = ix_bl;
    if (ix_br < min_ix) min_ix = ix_br;
    
    int max_ix = ix_tl;
    if (ix_tr > max_ix) max_ix = ix_tr;
    if (ix_bl > max_ix) max_ix = ix_bl;
    if (ix_br > max_ix) max_ix = ix_br;
    
    // Encontrar min/max de y
    int min_iy = iy_tl;
    if (iy_tr < min_iy) min_iy = iy_tr;
    if (iy_bl < min_iy) min_iy = iy_bl;
    if (iy_br < min_iy) min_iy = iy_br;
    
    int max_iy = iy_tl;
    if (iy_tr > max_iy) max_iy = iy_tr;
    if (iy_bl > max_iy) max_iy = iy_bl;
    if (iy_br > max_iy) max_iy = iy_br;
    
    *x_start = (min_ix >= 0) ? (unsigned int)min_ix : 0;
    *y_start = (min_iy >= 0) ? (unsigned int)min_iy : 0;
    *width = (max_ix > min_ix) ? (unsigned int)(max_ix - min_ix) : 0;
    *height = (max_iy > min_iy) ? (unsigned int)(max_iy - min_iy) : 0;
}

int run_rgb(ArgParser* parser) { // This is the correct, single definition
    // Inicializar el logger aquí para que los mensajes DEBUG se muestren
    LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
    logger_init(log_level);
    LOG_DEBUG("Modo verboso activado para el comando 'rgb'.");

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
        LOG_ERROR("Modo '%s' no reconocido. Modos válidos: composite, truecolor, night, ash, airmass, so2.", mode);
        return -1;
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

    // Punteros para todos los canales posibles
    ChannelInfo* c_info[17] = {NULL}; // Índices 1-16
    for (int i = 0; i < channels->count; i++) {
        if (channels->channels[i].filename == NULL) {
            LOG_ERROR("Archivo faltante para el canal %s con ID %s en el directorio %s", channels->channels[i].name, channels->id_signature, dirnm);
            channelset_destroy(channels);
            return -1;
        }
        // Extraer el número de canal para un acceso genérico
        int channel_num = atoi(channels->channels[i].name + 1);
        if (channel_num > 0 && channel_num <= 16) {
            c_info[channel_num] = &channels->channels[i];
        }
    }

    DataNC c[17]; // Array para almacenar los datos de los canales
    DataF aux, navlo, navla;
    const char* var_name = is_l2_product ? "CMI" : "Rad";

    // Cargar solo los canales necesarios
    for (int i = 1; i <= 16; i++) {
        if (c_info[i]) {
            load_nc_sf(c_info[i]->filename, var_name, &c[i]);
        }
    }

    // Iguala los tamaños a la resolución mínima (la de C13)
    // El canal de referencia para la navegación y resolución es el de mayor número presente
    int ref_ch_idx = 0;
    for (int i = 16; i > 0; i--) { if (c_info[i]) { ref_ch_idx = i; break; } }
    if (ref_ch_idx == 0) { LOG_ERROR("No se encontró ningún canal de referencia."); return -1; }

    if (c_info[1]) { aux = downsample_boxfilter(c[1].fdata, 2); dataf_destroy(&c[1].fdata); c[1].fdata = aux; }
    if (c_info[2]) { aux = downsample_boxfilter(c[2].fdata, 4); dataf_destroy(&c[2].fdata); c[2].fdata = aux; }
    if (c_info[3]) { aux = downsample_boxfilter(c[3].fdata, 2); dataf_destroy(&c[3].fdata); c[3].fdata = aux; }
    
    compute_navigation_nc(c_info[ref_ch_idx]->filename, &navla, &navlo);
    
    // ¡CORRECCIÓN CRÍTICA! Remuestrear la navegación si los datos no están reproyectados
    // Y SI se usaron canales de alta resolución que fueron remuestreados.
    // El canal C02 (rojo) es el de mayor resolución (0.5km -> 2km, factor 4).
    // Si está presente, significa que la resolución final es de 2km.
    if (!do_reprojection && c_info[2]) {
        LOG_DEBUG("Remuestreando mallas de navegación para coincidir con datos de 2km.");
        aux = downsample_boxfilter(navla, 2); dataf_destroy(&navla); navla = aux;
        aux = downsample_boxfilter(navlo, 2); dataf_destroy(&navlo); navlo = aux;
    }

    // --- NUEVO: Si hay --clip y -r, primero recortamos en espacio geoestacionario ---
    unsigned int clip_x_start = 0, clip_y_start = 0;
    unsigned int clip_width = 0, clip_height = 0;
    bool has_clip = ap_found(parser, "clip") && (ap_count(parser, "clip") == 4);
    
    if (has_clip && do_reprojection) {
        float clip_lon_min = atof(ap_get_str_value_at_index(parser, "clip", 0));
        float clip_lat_max = atof(ap_get_str_value_at_index(parser, "clip", 1));
        float clip_lon_max = atof(ap_get_str_value_at_index(parser, "clip", 2));
        float clip_lat_min = atof(ap_get_str_value_at_index(parser, "clip", 3));

        LOG_INFO("Aplicando recorte PRE-reproyección: lon[%.3f, %.3f], lat[%.3f, %.3f]", 
                 clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);

        // Encontrar las 4 esquinas en el espacio geoestacionario
        int ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br;
        reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_min, &ix_tl, &iy_tl);
        reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_max, &ix_tr, &iy_tr);
        reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_min, &ix_bl, &iy_bl);
        reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_max, &ix_br, &iy_br);

        LOG_DEBUG("Píxeles pre-reproj: TL(%d,%d), TR(%d,%d), BL(%d,%d), BR(%d,%d)", 
                  ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br);

        // Calcular bounding box usando función auxiliar
        calculate_bounding_box(ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br,
                               &clip_x_start, &clip_y_start, &clip_width, &clip_height);

        LOG_INFO("Recortando datos pre-reproyección: start[%u, %u], size[%u, %u]", 
                 clip_x_start, clip_y_start, clip_width, clip_height);

        // Recortar todos los canales y la navegación
        for (int i = 1; i <= 16; i++) {
            if (c_info[i]) {
                DataF cropped = dataf_crop(&c[i].fdata, clip_x_start, clip_y_start, clip_width, clip_height);
                dataf_destroy(&c[i].fdata);
                c[i].fdata = cropped;
            }
        }
        
        DataF navla_cropped = dataf_crop(&navla, clip_x_start, clip_y_start, clip_width, clip_height);
        DataF navlo_cropped = dataf_crop(&navlo, clip_x_start, clip_y_start, clip_width, clip_height);
        dataf_destroy(&navla);
        dataf_destroy(&navlo);
        navla = navla_cropped;
        navlo = navlo_cropped;
    }

    if (do_reprojection) {
        LOG_INFO("Iniciando reproyección para todos los canales...");
        float lon_min, lon_max, lat_min, lat_max;

        // Decidir si usar navegación pre-calculada (caso con clip) o calcular desde archivo
        if (has_clip) {
            // Caso con recorte pre-reproyección: usar navegación ya recortada
            bool first_repro = true;
            for (int i = 1; i <= 16; i++) {
                if (c_info[i]) {
                    DataF repro;
                    if (first_repro) {
                        repro = reproject_to_geographics_with_nav(&c[i].fdata, &navla, &navlo, 
                                                                   &lon_min, &lon_max, &lat_min, &lat_max);
                        first_repro = false;
                    } else {
                        repro = reproject_to_geographics_with_nav(&c[i].fdata, &navla, &navlo, 
                                                                   NULL, NULL, NULL, NULL);
                    }
                    dataf_destroy(&c[i].fdata);
                    c[i].fdata = repro;
                }
            }
        } else {
            // Caso sin recorte: calcular navegación desde archivo NetCDF
            const char* nav_ref = c_info[ref_ch_idx]->filename;
            bool first_repro = true;
            for (int i = 1; i <= 16; i++) {
                if (c_info[i]) {
                    DataF repro;
                    if (first_repro) {
                        repro = reproject_to_geographics(&c[i].fdata, nav_ref, &lon_min, &lon_max, &lat_min, &lat_max);
                        first_repro = false;
                    } else {
                        repro = reproject_to_geographics(&c[i].fdata, nav_ref, NULL, NULL, NULL, NULL);
                    }
                    dataf_destroy(&c[i].fdata);
                    c[i].fdata = repro;
                }
            }
        }

        // Liberar la navegación original (geostacionaria)
        dataf_destroy(&navla);
        dataf_destroy(&navlo);

        // Crear la nueva navegación directamente sobre la malla geográfica, sin reproyectar.
        size_t final_width = c[ref_ch_idx].fdata.width;
        size_t final_height = c[ref_ch_idx].fdata.height;
        LOG_INFO("Creando navegación para la malla geográfica final...");
        create_navigation_from_reprojected_bounds(&navla, &navlo, final_width, final_height, lon_min, lon_max, lat_min, lat_max);

        LOG_INFO("Reproyección completada.");
    }

    const char* out_filename = ap_get_str_value(parser, "out");
    ImageData final_image;
    // Variables para las bandas intermedias que necesitan ser liberadas
    DataF r_ch = {0}, g_ch = {0}, b_ch = {0};

    if (strcmp(mode, "truecolor") == 0) {
        LOG_INFO("Generando imagen en modo 'truecolor'...");
        ImageData diurna = create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
        image_apply_histogram(diurna);
        if (gamma != 1.0) image_apply_gamma(diurna, gamma);
        write_image_png(out_filename, &diurna);
        final_image = diurna;

    } else if (strcmp(mode, "night") == 0) {
        LOG_INFO("Generando imagen en modo 'night'...");
        ImageData nocturna = create_nocturnal_pseudocolor(c[13]);
        if (gamma != 1.0) image_apply_gamma(nocturna, gamma);
        final_image = nocturna;

    } else if (strcmp(mode, "ash") == 0) {
        LOG_INFO("Generando imagen en modo 'ash'...");
        r_ch = dataf_op_dataf(&c[15].fdata, &c[13].fdata, OP_SUB);
        g_ch = dataf_op_dataf(&c[14].fdata, &c[11].fdata, OP_SUB);
        final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -6.7f, 2.6f, -6.0f, 6.3f, 243.6f, 302.4f);

    } else if (strcmp(mode, "airmass") == 0) {
        LOG_INFO("Generando imagen en modo 'airmass'...");
        r_ch = dataf_op_dataf(&c[8].fdata, &c[10].fdata, OP_SUB);
        g_ch = dataf_op_dataf(&c[12].fdata, &c[13].fdata, OP_SUB);
        b_ch = dataf_op_scalar(&c[8].fdata, 273.15f, OP_SUB, true); // scalar_first = true -> 273.15 - C8
        final_image = create_multiband_rgb(&r_ch, &g_ch, &b_ch, -26.2f, 0.6f, -43.2f, 6.7f, 
            29.25f, 64.65f);

    } else if (strcmp(mode, "so2") == 0) {
        LOG_INFO("Generando imagen en modo 'so2'...");
        r_ch = dataf_op_dataf(&c[9].fdata, &c[10].fdata, OP_SUB);
        g_ch = dataf_op_dataf(&c[13].fdata, &c[11].fdata, OP_SUB);
        final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -4.0f, 2.0f, -4.0f, 5.0f, 233.0f, 300.0f);

    } else { // Modo "composite" (default)
        LOG_INFO("Generando imagen en modo 'composite'...");
        ImageData diurna = create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
        image_apply_histogram(diurna);
        ImageData nocturna = create_nocturnal_pseudocolor(c[13]);

        float dnratio;
        ImageData mask = create_daynight_mask(c[13], navla, navlo, &dnratio, 263.15);
        LOG_INFO("Ratio día/noche: %.2f%%", dnratio);

        ImageData blend = blend_images(nocturna, diurna, mask);
        if (gamma != 1.0) image_apply_gamma(blend, gamma);
        final_image = blend;
        image_destroy(&diurna);
        image_destroy(&nocturna);
        image_destroy(&mask);
    }

    // --- LÓGICA DE RECORTE FINAL ---
    // Solo aplicar si hay --clip pero NO se hizo recorte pre-reproyección
    // (es decir, solo para el caso sin -r)
    if (has_clip && !do_reprojection) {
        if (navla.data_in) {
            float clip_lon_min = atof(ap_get_str_value_at_index(parser, "clip", 0));
            float clip_lat_max = atof(ap_get_str_value_at_index(parser, "clip", 1));
            float clip_lon_max = atof(ap_get_str_value_at_index(parser, "clip", 2));
            float clip_lat_min = atof(ap_get_str_value_at_index(parser, "clip", 3));

            LOG_INFO("Aplicando recorte POST-procesamiento (sin reproyección): lon[%.3f, %.3f], lat[%.3f, %.3f]", 
                     clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);

            // Datos originales (cuadrícula geoestacionaria) - búsqueda de píxeles
            int ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br;
            reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_min, &ix_tl, &iy_tl);
            reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_max, &ix_tr, &iy_tr);
            reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_min, &ix_bl, &iy_bl);
            reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_max, &ix_br, &iy_br);

            LOG_DEBUG("Píxeles de las esquinas: TL(%d,%d), TR(%d,%d), BL(%d,%d), BR(%d,%d)", 
                      ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br);

            // Calcular bounding box usando función auxiliar
            unsigned int x_start, y_start, crop_width, crop_height;
            calculate_bounding_box(ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br,
                                   &x_start, &y_start, &crop_width, &crop_height);

            LOG_INFO("Dimensiones de recorte en píxeles: start[%u, %u], size[%u, %u]", 
                     x_start, y_start, crop_width, crop_height);

            ImageData cropped_image = image_crop(&final_image, x_start, y_start, crop_width, crop_height);
            if (cropped_image.data) {
                image_destroy(&final_image);
                final_image = cropped_image;
            }
        } else {
            LOG_WARN("Formato de coordenadas de recorte inválido. Se esperaba \"lon_min lat_max lon_max lat_min\".");
        }
    }

    write_image_png(out_filename, &final_image);
    LOG_INFO("Imagen guardada en: %s", out_filename);

    // Liberar las bandas intermedias creadas para los modos especiales
    dataf_destroy(&r_ch);
    dataf_destroy(&g_ch);
    dataf_destroy(&b_ch);
    
    image_destroy(&final_image);
    for (int i = 1; i <= 16; i++) {
        if (c_info[i]) datanc_destroy(&c[i]);
    }
    dataf_destroy(&navla); dataf_destroy(&navlo);
    channelset_destroy(channels);
    return 0;
}