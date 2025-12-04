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
#include <limits.h>
#include <stddef.h>
#include <omp.h>
#include <math.h>
#include <stdlib.h>

/**
 * @brief Reproyecta una malla DataF de coordenadas geoestacionarias a geográficas.
 *
 * @param source_data La malla de datos de origen a reproyectar.
 * @param nav_reference_file Ruta a un archivo NetCDF para calcular la malla de navegación (lat/lon).
 * @param native_resolution_km Resolución nativa del sensor en km (0 para usar default de 1 km).
 * @return Una nueva estructura DataF con los datos reproyectados y con huecos rellenados.
 *         El llamador es responsable de liberar esta estructura con dataf_destroy().
 *         Retorna una DataF vacía en caso de fallo. Los parámetros out_* contendrán los
 *         límites geográficos de la malla reproyectada.
 */
DataF reproject_to_geographics(const DataF* source_data, const char* nav_reference_file,
                               float native_resolution_km,
                               float* out_lon_min, float* out_lon_max,
                               float* out_lat_min, float* out_lat_max) {
    LOG_INFO("Iniciando reproyección a geográficas...");

    DataF navla, navlo;
    if (compute_navigation_nc(nav_reference_file, &navla, &navlo) != 0) {
        LOG_ERROR("Falla al calcular la navegación para la reproyección.");
        return dataf_create(0, 0);
    }
    
    DataF result = reproject_to_geographics_with_nav(source_data, &navla, &navlo, 
                                                      native_resolution_km,
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
 * @param native_resolution_km Resolución nativa del sensor en km (0 para usar default de 1 km).
 * @return Una nueva estructura DataF con los datos reproyectados y con huecos rellenados.
 */
DataF reproject_to_geographics_with_nav(const DataF* source_data, const DataF* navla, const DataF* navlo,
                                        float native_resolution_km,
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

    // Calcular tamaño de salida basado en resolución geográfica deseada
    float lon_range = navlo->fmax - navlo->fmin;
    float lat_range = navla->fmax - navla->fmin;
    
    // Calcular la latitud central para corrección geográfica precisa
    float lat_center = (navla->fmin + navla->fmax) / 2.0f;
    float lat_rad = lat_center * (M_PI / 180.0f);
    
    // Fórmula WGS84 para km/grado (más precisa que constante 111)
    // Usamos solo km_per_deg_lat para obtener resolución "cuadrada" en grados
    // (igual que hace GDAL)
    float km_per_deg_lat = 111.132954f - 0.559822f * cosf(2.0f * lat_rad);
    
    // Usar la resolución nativa del sensor (del atributo spatial_resolution)
    // Si no está disponible, usar 1.0 km como default razonable para GOES-R ABI
    float target_res_km = (native_resolution_km > 0.0f) ? native_resolution_km : 1.0f;
    
    // Resolución en grados (cuadrada, igual para lon y lat)
    float target_res_deg = target_res_km / km_per_deg_lat;
    
    LOG_INFO("Resolución objetivo: %.3f km = %.6f° (nativa del sensor), lat_center=%.2f°, "
             "km/deg_lat=%.3f",
             target_res_km, target_res_deg, lat_center, km_per_deg_lat);
    
    // Calcular dimensiones de salida usando la misma resolución en grados para ambos ejes
    size_t width = (size_t)(lon_range / target_res_deg + 0.5f);
    size_t height = (size_t)(lat_range / target_res_deg + 0.5f);
    
    // Asegurar dimensiones mínimas razonables
    if (width < 10) width = 10;
    if (height < 10) height = 10;
    
    // Limitar dimensiones máximas para evitar consumo excesivo de memoria
    const size_t MAX_DIM = 10000;
    if (width > MAX_DIM) {
        LOG_WARN("Ancho calculado (%zu) excede el máximo (%zu). Limitando.", width, MAX_DIM);
        width = MAX_DIM;
    }
    if (height > MAX_DIM) {
        LOG_WARN("Alto calculado (%zu) excede el máximo (%zu). Limitando.", height, MAX_DIM);
        height = MAX_DIM;
    }
    
    LOG_INFO("Tamaño de salida calculado: %zu x %zu (entrada: %zu x %zu, res objetivo: %.2f km)", 
             width, height, navlo->width, navla->height, target_res_km);

    DataF datagg = dataf_create(width, height);
    if (datagg.data_in == NULL) {
        LOG_FATAL("Falla de memoria al crear la malla geográfica de destino.");
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
        LOG_FATAL("Falla de memoria al copiar la malla para el relleno de huecos.");
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

/**
 * @brief Calcula el bounding box para un dominio geográfico usando muestreo denso de bordes.
 */
int reprojection_find_bounding_box(const DataF* navla, const DataF* navlo,
                                   float clip_lon_min, float clip_lat_max,
                                   float clip_lon_max, float clip_lat_min,
                                   int* out_x_start, int* out_y_start,
                                   int* out_width, int* out_height) {
    const int SAMPLES_PER_EDGE = 20;
    int min_ix = INT_MAX, max_ix = INT_MIN;
    int min_iy = INT_MAX, max_iy = INT_MIN;
    int valid_samples = 0;
    
    // Muestrear los 4 bordes del dominio geográfico
    for (int s = 0; s <= SAMPLES_PER_EDGE; s++) {
        float t = (float)s / (float)SAMPLES_PER_EDGE;
        int ix, iy;
        
        // Borde SUPERIOR (lat_max, lon varía)
        float lon = clip_lon_min + t * (clip_lon_max - clip_lon_min);
        reprojection_find_pixel_for_coord(navla, navlo, clip_lat_max, lon, &ix, &iy);
        if (ix >= 0 && iy >= 0) {
            if (ix < min_ix) min_ix = ix;
            if (ix > max_ix) max_ix = ix;
            if (iy < min_iy) min_iy = iy;
            if (iy > max_iy) max_iy = iy;
            valid_samples++;
        }
        
        // Borde INFERIOR (lat_min, lon varía)
        reprojection_find_pixel_for_coord(navla, navlo, clip_lat_min, lon, &ix, &iy);
        if (ix >= 0 && iy >= 0) {
            if (ix < min_ix) min_ix = ix;
            if (ix > max_ix) max_ix = ix;
            if (iy < min_iy) min_iy = iy;
            if (iy > max_iy) max_iy = iy;
            valid_samples++;
        }
        
        // Borde IZQUIERDO (lon_min, lat varía)
        float lat = clip_lat_min + t * (clip_lat_max - clip_lat_min);
        reprojection_find_pixel_for_coord(navla, navlo, lat, clip_lon_min, &ix, &iy);
        if (ix >= 0 && iy >= 0) {
            if (ix < min_ix) min_ix = ix;
            if (ix > max_ix) max_ix = ix;
            if (iy < min_iy) min_iy = iy;
            if (iy > max_iy) max_iy = iy;
            valid_samples++;
        }
        
        // Borde DERECHO (lon_max, lat varía)
        reprojection_find_pixel_for_coord(navla, navlo, lat, clip_lon_max, &ix, &iy);
        if (ix >= 0 && iy >= 0) {
            if (ix < min_ix) min_ix = ix;
            if (ix > max_ix) max_ix = ix;
            if (iy < min_iy) min_iy = iy;
            if (iy > max_iy) max_iy = iy;
            valid_samples++;
        }
    }
    
    // Devolver resultados
    if (valid_samples >= 4 && min_ix < INT_MAX && min_iy < INT_MAX) {
        *out_x_start = min_ix;
        *out_y_start = min_iy;
        *out_width = max_ix - min_ix + 1;
        *out_height = max_iy - min_iy + 1;
    } else {
        *out_x_start = 0;
        *out_y_start = 0;
        *out_width = 0;
        *out_height = 0;
    }
    
    return valid_samples;
}