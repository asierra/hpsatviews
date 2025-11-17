/*
 * Geostationary to Geographics Reprojection Module
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */

#include "reprojection.h"
#include "datanc.h"
#include "reader_nc.h"
#include "logger.h"
#include <float.h>
#include <stddef.h>
#include <omp.h>
#include <math.h>
#include <stdlib.h>

/**
 * @brief Reproyecta una malla DataF de coordenadas geoestacionarias a geográficas.
 *
 * @param source_data La malla de datos de origen a reproyectar.
 * @param nav_reference_file Ruta a un archivo NetCDF para calcular la malla de navegación (lat/lon).
 * @return Una nueva estructura DataF con los datos reproyectados y con huecos rellenados.
 *         El llamador es responsable de liberar esta estructura con dataf_destroy().
 *         Retorna una DataF vacía en caso de fallo. Los parámetros out_* contendrán los
 *         límites geográficos de la malla reproyectada.
 */
DataF reproject_to_geographics(const DataF* source_data, const char* nav_reference_file,
                               float* out_lon_min, float* out_lon_max,
                               float* out_lat_min, float* out_lat_max) {
    LOG_INFO("Iniciando reproyección a geográficas...");

    DataF navla, navlo;
    if (compute_navigation_nc(nav_reference_file, &navla, &navlo) != 0) {
        LOG_ERROR("Fallo al calcular la navegación para la reproyección.");
        return dataf_create(0, 0);
    }
    
    DataF result = reproject_to_geographics_with_nav(source_data, &navla, &navlo, 
                                                      out_lon_min, out_lon_max, 
                                                      out_lat_min, out_lat_max);
    
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    
    return result;
}

/**
 * @brief Reproyecta una malla DataF usando navegación pre-calculada.
 *
 * @param source_data La malla de datos de origen a reproyectar.
 * @param navla Puntero a la malla de latitudes pre-calculada.
 * @param navlo Puntero a la malla de longitudes pre-calculada.
 * @return Una nueva estructura DataF con los datos reproyectados y con huecos rellenados.
 */
DataF reproject_to_geographics_with_nav(const DataF* source_data, const DataF* navla, const DataF* navlo,
                                        float* out_lon_min, float* out_lon_max,
                                        float* out_lat_min, float* out_lat_max) {
    if (!source_data || !navla || !navlo || !navla->data_in || !navlo->data_in) {
        LOG_ERROR("Parámetros inválidos para reproyección.");
        return dataf_create(0, 0);
    }
    
    LOG_INFO("Datos de navegación pre-calculada. Extensión: lat[%.3f, %.3f], lon[%.3f, %.3f]", 
             navla->fmin, navla->fmax, navlo->fmin, navlo->fmax);
    // Devolver los límites geográficos calculados
    if (out_lon_min) *out_lon_min = navlo->fmin;
    if (out_lon_max) *out_lon_max = navlo->fmax;
    if (out_lat_min) *out_lat_min = navla->fmin;
    if (out_lat_max) *out_lat_max = navla->fmax;

    // Calcular tamaño de salida basado en la resolución aproximada
    // Usamos un factor que mantiene aproximadamente la misma densidad de píxeles
    // que la imagen original en términos de grados por píxel
    float lon_range = navlo->fmax - navlo->fmin;
    float lat_range = navla->fmax - navla->fmin;
    
    // Calcular la resolución promedio de la entrada (grados por píxel)
    float input_lon_res = lon_range / (float)navlo->width;
    float input_lat_res = lat_range / (float)navla->height;
    
    // Para la salida, usar una resolución similar o ligeramente mejor
    size_t width = (size_t)(lon_range / input_lon_res);
    size_t height = (size_t)(lat_range / input_lat_res);
    
    LOG_INFO("Tamaño de salida calculado: %zu x %zu (entrada: %zu x %zu)", 
             width, height, navlo->width, navla->height);

    DataF datagg = dataf_create(width, height);
    if (datagg.data_in == NULL) {
        LOG_FATAL("Fallo de memoria al crear la malla geográfica de destino.");
        return datagg;
    }
    dataf_fill(&datagg, NAN);

    // Pre-calcular factores de escala para evitar divisiones repetidas
    float lon_scale = (width - 1) / (navlo->fmax - navlo->fmin);
    float lat_scale = (height - 1) / (navla->fmax - navla->fmin);

    #pragma omp parallel for collapse(2)
    for (unsigned y = 0; y < source_data->height; y++) {
        for (unsigned x = 0; x < source_data->width; x++) {
            unsigned i = y * source_data->width + x;
            float lo = navlo->data_in[i];
            float la = navla->data_in[i];
            float f = source_data->data_in[i];
            if (lo != NonData && la != NonData && f != NonData) {
                int ix = (int)((lo - navlo->fmin) * lon_scale);
                int iy = (int)((navla->fmax - la) * lat_scale);

                if (ix >= 0 && ix < (int)width && iy >= 0 && iy < (int)height) {
                    size_t j = (size_t)iy * width + (size_t)ix;
                    // OMP atomic needs a direct memory reference of a known type
                    #pragma omp atomic write
                    datagg.data_in[j] = f;
                }
            }
        }
    }
    LOG_DEBUG("Bucle de reproyección terminado.");
    datagg.fmin = source_data->fmin;
    datagg.fmax = source_data->fmax;

    LOG_INFO("Iniciando relleno de huecos (interpolación de vecinos)...");
    DataF datagg_filled = dataf_copy(&datagg);
    if (datagg_filled.data_in == NULL) {
        LOG_FATAL("Fallo de memoria al copiar la malla para el relleno de huecos.");
        dataf_destroy(&datagg);
        return dataf_create(0, 0);
    }

    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};
    const int num_neighbors = 4;

    #pragma omp parallel for collapse(2)
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t idx = y * width + x;
            if (isnan(datagg.data_in[idx])) {
                float sum = 0.0f;
                int count = 0;
                for (int k = 0; k < num_neighbors; k++) {
                    int nx = (int)x + dx[k];
                    int ny = (int)y + dy[k];
                    if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                        size_t n_idx = (size_t)ny * width + (size_t)nx;
                        float n_val = datagg.data_in[n_idx];
                        if (!isnan(n_val)) {
                            sum += n_val;
                            count++;
                        }
                    }
                }
                if (count > 0) {
                    datagg_filled.data_in[idx] = sum / (float)count;
                }
            }
        }
    }
    LOG_INFO("Relleno de huecos terminado.");

    // Liberar memoria intermedia
    dataf_destroy(&datagg);

    // Devolvemos la malla rellena. El que llama es responsable de liberarla.
    return datagg_filled;
}

/**
 * @brief Encuentra el píxel más cercano a una coordenada geográfica dada en una malla no reproyectada.
 *
 * Esta función busca en las mallas de navegación de latitud y longitud para encontrar el índice
 * del píxel (columna, fila) que está geográficamente más cerca de las coordenadas de destino.
 * Utiliza OpenMP para paralelizar la búsqueda por filas.
 *
 * @param navla Puntero a una estructura DataF que contiene la latitud para cada píxel.
 * @param navlo Puntero a una estructura DataF que contiene la longitud para cada píxel.
 * @param target_lat La latitud de la coordenada de destino.
 * @param target_lon La longitud de la coordenada de destino.
 * @param out_ix Puntero a un entero donde se almacenará el índice de la columna (x) resultante.
 * @param out_iy Puntero a un entero donde se almacenará el índice de la fila (y) resultante.
 */
void reprojection_find_pixel_for_coord(const DataF* navla, const DataF* navlo,
                                       float target_lat, float target_lon,
                                       int* out_ix, int* out_iy) {
    if (!navla || !navla->data_in || !navlo || !navlo->data_in || !out_ix || !out_iy) {
        if (out_ix) *out_ix = -1;
        if (out_iy) *out_iy = -1;
        LOG_WARN("reprojection_find_pixel_for_coord: parámetros inválidos");
        return;
    }

    size_t width = navla->width;
    size_t height = navla->height;

    float min_dist_sq = FLT_MAX;
    int best_ix = -1;
    int best_iy = -1;
    int candidates_checked = 0;
    int valid_pixels_found = 0;

    #pragma omp parallel
    {
        float local_min_dist_sq = FLT_MAX;
        int local_best_ix = -1;
        int local_best_iy = -1;
        int local_checked = 0;
        int local_valid = 0;

        #pragma omp for nowait
        for (size_t j = 0; j < height; ++j) {
            for (size_t i = 0; i < width; ++i) {
                size_t index = j * width + i;
                float current_lat = navla->data_in[index];
                float current_lon = navlo->data_in[index];

                local_checked++;
                
                // ¡CORRECCIÓN! Ignorar píxeles inválidos en la malla de navegación.
                if (current_lat == NonData || current_lon == NonData) {
                    continue;
                }
                
                local_valid++;
                float lat_diff = current_lat - target_lat;
                float lon_diff = current_lon - target_lon;
                float dist_sq = lat_diff * lat_diff + lon_diff * lon_diff;

                if (dist_sq < local_min_dist_sq) {
                    local_min_dist_sq = dist_sq;
                    local_best_ix = (int)i;
                    local_best_iy = (int)j;
                }
            }
        }

        #pragma omp critical
        {
            candidates_checked += local_checked;
            valid_pixels_found += local_valid;
            
            if (local_min_dist_sq < min_dist_sq) {
                min_dist_sq = local_min_dist_sq;
                best_ix = local_best_ix;
                best_iy = local_best_iy;
            }
        }
    }

    if (valid_pixels_found == 0) {
        LOG_WARN("reprojection_find_pixel_for_coord: no se encontraron píxeles válidos (checked=%d, target=[%.3f, %.3f])", 
                 candidates_checked, target_lat, target_lon);
    } else if (best_ix == -1 || best_iy == -1) {
        LOG_WARN("reprojection_find_pixel_for_coord: búsqueda falló (valid=%d/%d, target=[%.3f, %.3f])", 
                 valid_pixels_found, candidates_checked, target_lat, target_lon);
    } else {
        LOG_DEBUG("reprojection_find_pixel_for_coord: encontrado píxel [%d, %d] para coord [%.3f, %.3f] (dist=%.6f, valid=%d/%d)",
                  best_ix, best_iy, target_lat, target_lon, sqrtf(min_dist_sq), valid_pixels_found, candidates_checked);
    }

    *out_ix = best_ix;
    *out_iy = best_iy;
}