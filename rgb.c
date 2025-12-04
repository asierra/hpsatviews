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

/*
 * NOTA: Las funciones infer_missing_corners() y calculate_bounding_box() fueron 
 * reemplazadas por una estrategia mejorada de MUESTREO DENSO DE BORDES.
 * 
 * La estrategia anterior solo evaluaba las 4 esquinas del dominio geográfico,
 * lo cual generaba un bounding box rectangular que no capturaba correctamente
 * la deformación del rectángulo geográfico al mapear al espacio geoestacionario.
 * 
 * La nueva estrategia muestrea múltiples puntos (20 por defecto) a lo largo de
 * cada uno de los 4 bordes del dominio geográfico, encontrando así el verdadero
 * bounding box que contiene TODOS los píxeles del dominio, no solo sus esquinas.
 * 
 * Ver las implementaciones en las secciones de clipping PRE y POST reproyección.
 */

int run_rgb(ArgParser* parser) { // This is the correct, single definition
    // Inicializar el logger aquí para que los mensajes DEBUG se muestren
    LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
    logger_init(log_level);
    LOG_DEBUG("Modo verboso activado para el comando 'rgb'.");

    const char* input_file;
    bool do_reprojection = ap_found(parser, "geographics");
    float gamma = ap_get_dbl_value(parser, "gamma");
    const char* mode = ap_get_str_value(parser, "mode");
    bool apply_histogram = ap_found(parser, "histo");
    int scale = ap_get_int_value(parser, "scale");
    bool use_alpha = ap_found(parser, "alpha");


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

        // ESTRATEGIA MEJORADA: Muestrear densamente los bordes del dominio geográfico
        // En lugar de solo 4 esquinas, evaluamos múltiples puntos a lo largo de cada borde.
        // Esto asegura que capturamos toda la deformación del rectángulo geográfico
        // cuando se mapea al espacio geoestacionario.
        
        int clip_x_start_int, clip_y_start_int, clip_w, clip_h;
        int valid_samples = reprojection_find_bounding_box(&navla, &navlo,
                                                             clip_lon_min, clip_lat_max,
                                                             clip_lon_max, clip_lat_min,
                                                             &clip_x_start_int, &clip_y_start_int,
                                                             &clip_w, &clip_h);
        
        if (valid_samples < 4) {
            LOG_ERROR("Dominio de clip fuera del disco visible (solo %d muestras válidas).", 
                     valid_samples);
            clip_width = 0;
            clip_height = 0;
        } else {
            clip_x_start = (unsigned int)clip_x_start_int;
            clip_y_start = (unsigned int)clip_y_start_int;
            clip_width = (unsigned int)clip_w;
            clip_height = (unsigned int)clip_h;
            
            LOG_INFO("Bounding box calculado desde %d muestras válidas: start[%u, %u], size[%u, %u]",
                     valid_samples, clip_x_start, clip_y_start, clip_width, clip_height);
        }

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
                                                                   c[i].native_resolution_km,
                                                                   &lon_min, &lon_max, &lat_min, &lat_max);
                        first_repro = false;
                    } else {
                        repro = reproject_to_geographics_with_nav(&c[i].fdata, &navla, &navlo, 
                                                                   c[i].native_resolution_km,
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
                        repro = reproject_to_geographics(&c[i].fdata, nav_ref, 
                                                          c[i].native_resolution_km,
                                                          &lon_min, &lon_max, &lat_min, &lat_max);
                        first_repro = false;
                    } else {
                        repro = reproject_to_geographics(&c[i].fdata, nav_ref, 
                                                          c[i].native_resolution_km,
                                                          NULL, NULL, NULL, NULL);
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
        
        // RECORTE POST-REPROYECCIÓN: Eliminar píxeles fuera del dominio solicitado
        if (has_clip) {
            float clip_lon_min = atof(ap_get_str_value_at_index(parser, "clip", 0));
            float clip_lat_max = atof(ap_get_str_value_at_index(parser, "clip", 1));
            float clip_lon_max = atof(ap_get_str_value_at_index(parser, "clip", 2));
            float clip_lat_min = atof(ap_get_str_value_at_index(parser, "clip", 3));
            
            LOG_INFO("Aplicando recorte POST-reproyección al dominio solicitado: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                     clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);
            
            // Los datos están en una malla geográfica regular, podemos usar interpolación lineal
            float lon_range = lon_max - lon_min;
            float lat_range = lat_max - lat_min;
            
            // Calcular índices usando la extensión de la reproyección
            int ix_start = (int)(((clip_lon_min - lon_min) / lon_range) * final_width);
            int iy_start = (int)(((lat_max - clip_lat_max) / lat_range) * final_height);
            int ix_end = (int)(((clip_lon_max - lon_min) / lon_range) * final_width);
            int iy_end = (int)(((lat_max - clip_lat_min) / lat_range) * final_height);
            
            // Asegurar que estén dentro de los límites
            if (ix_start < 0) ix_start = 0;
            if (iy_start < 0) iy_start = 0;
            if (ix_end > (int)final_width) ix_end = final_width;
            if (iy_end > (int)final_height) iy_end = final_height;
            
            unsigned int crop_x = (unsigned int)ix_start;
            unsigned int crop_y = (unsigned int)iy_start;
            unsigned int crop_w = (unsigned int)(ix_end - ix_start);
            unsigned int crop_h = (unsigned int)(iy_end - iy_start);
            
            LOG_INFO("Recorte POST-reproj: start[%u, %u], size[%u, %u]", crop_x, crop_y, crop_w, crop_h);
            
            // Recortar todos los canales reproyectados
            for (int i = 1; i <= 16; i++) {
                if (c_info[i]) {
                    DataF cropped = dataf_crop(&c[i].fdata, crop_x, crop_y, crop_w, crop_h);
                    dataf_destroy(&c[i].fdata);
                    c[i].fdata = cropped;
                }
            }
            
            // Actualizar navegación para reflejar el nuevo dominio
            dataf_destroy(&navla);
            dataf_destroy(&navlo);
            create_navigation_from_reprojected_bounds(&navla, &navlo, crop_w, crop_h, 
                                                     clip_lon_min, clip_lon_max, clip_lat_min, clip_lat_max);
        }
    }

    char* out_filename_generated = NULL;
    const char* out_filename;

    if (ap_found(parser, "out")) {
        out_filename = ap_get_str_value(parser, "out");
    } else {
        // Generar nombre de archivo por defecto usando el canal de referencia,
        // que garantiza que el archivo existe y está siendo usado en este modo.
        // Usar 'input_file' podría ser incorrecto si el modo no usa ese canal.
        const char* filename_for_timestamp = c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : input_file;
        char* base_filename = generate_default_output_filename(filename_for_timestamp, mode, ".png");
        
        // Si hay reproyección, agregar sufijo _geo antes de la extensión
        if (do_reprojection && base_filename) {
            // Buscar la posición del último punto (extensión)
            char* dot = strrchr(base_filename, '.');
            if (dot) {
                size_t prefix_len = dot - base_filename;
                size_t total_len = strlen(base_filename) + 5; // +4 para "_geo" +1 para '\0'
                out_filename_generated = (char*)malloc(total_len);
                if (out_filename_generated) {
                    // Copiar la parte antes de la extensión
                    strncpy(out_filename_generated, base_filename, prefix_len);
                    out_filename_generated[prefix_len] = '\0';
                    // Agregar sufijo y extensión
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
        
        out_filename = out_filename_generated;
    }

    ImageData final_image;
    // Variables para las bandas intermedias que necesitan ser liberadas
    DataF r_ch = {0}, g_ch = {0}, b_ch = {0};
    
    // Determinar si se debe aplicar corrección de Rayleigh
    bool apply_rayleigh = ap_found(parser, "rayleigh");

    if (strcmp(mode, "truecolor") == 0) {
        LOG_INFO("Generando imagen en modo 'truecolor'...");
        ImageData diurna;
        if (apply_rayleigh) {
            LOG_INFO("Aplicando corrección atmosférica de Rayleigh...");
            diurna = create_truecolor_rgb_rayleigh(c[1].fdata, c[2].fdata, c[3].fdata, c_info[1]->filename, true);
        } else {
            diurna = create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
        }
        if (apply_histogram) image_apply_histogram(diurna);
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
        ImageData diurna;
        if (apply_rayleigh) {
            LOG_INFO("Aplicando corrección atmosférica de Rayleigh...");
            diurna = create_truecolor_rgb_rayleigh(c[1].fdata, c[2].fdata, c[3].fdata, c_info[1]->filename, true);
        } else {
            diurna = create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
        }
        if (apply_histogram) image_apply_histogram(diurna);
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

            // Usar función compartida de muestreo denso
            int x_start_int, y_start_int, crop_w, crop_h;
            int valid_samples = reprojection_find_bounding_box(&navla, &navlo,
                                                                 clip_lon_min, clip_lat_max,
                                                                 clip_lon_max, clip_lat_min,
                                                                 &x_start_int, &y_start_int,
                                                                 &crop_w, &crop_h);
            
            unsigned int x_start = 0, y_start = 0, crop_width = 0, crop_height = 0;
            
            if (valid_samples < 4) {
                LOG_WARN("Dominio de clip fuera del disco (solo %d muestras válidas). Ignorando recorte POST-procesamiento.",
                         valid_samples);
            } else {
                x_start = (unsigned int)x_start_int;
                y_start = (unsigned int)y_start_int;
                crop_width = (unsigned int)crop_w;
                crop_height = (unsigned int)crop_h;
                
                LOG_INFO("Bounding box desde %d muestras válidas: start[%u, %u], size[%u, %u]",
                         valid_samples, x_start, y_start, crop_width, crop_height);
            }

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
    
    // TODO: Implementar soporte para --scale y --alpha en modo RGB
    // El escalado requiere funciones de interpolación para ImageData
    // El canal alfa requiere cambiar bpp de 3 a 4 y gestionar transparencia
    if (scale != 1) {
        LOG_WARN("La opción --scale aún no está implementada para el comando rgb.");
    }
    if (use_alpha) {
        LOG_WARN("La opción --alpha aún no está implementada para el comando rgb.");
    }

    // Liberar las bandas intermedias creadas para los modos especiales
    dataf_destroy(&r_ch);
    dataf_destroy(&g_ch);
    dataf_destroy(&b_ch);
    
    if (out_filename_generated) free(out_filename_generated);
    image_destroy(&final_image);
    for (int i = 1; i <= 16; i++) {
        if (c_info[i]) datanc_destroy(&c[i]);
    }
    dataf_destroy(&navla); dataf_destroy(&navlo);
    channelset_destroy(channels);
    return 0;
}