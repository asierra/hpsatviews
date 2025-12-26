/*
 * Geostationary to Geographics Reprojection Module
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
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
#include <string.h>
#include <stdlib.h>

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
 * @brief Reproyecta una imagen de su proyección nativa a coordenadas geográficas.
 *
 * Esta función es similar a `reproject_to_geographics_with_nav` pero opera sobre
 * una `ImageData` de 8 bits en lugar de una `DataF` de floats. Esto es más eficiente
 * si la conversión a 8 bits ya se ha realizado.
 *
 * @param src_image La imagen de origen (en proyección geoestacionaria).
 * @param navla Malla de latitudes correspondiente a la imagen de origen.
 * @param navlo Malla de longitudes correspondiente a la imagen de origen.
 * @param native_resolution_km Resolución nativa del sensor en km.
 * @param clip_coords Opcional, para recortar la salida a un dominio geográfico.
 * @return Una nueva ImageData reproyectada. El llamador debe liberarla.
 */
ImageData reproject_image_to_geographics(const ImageData* src_image, const DataF* navla, const DataF* navlo,
                                         float native_resolution_km, const float* clip_coords) {
    if (!src_image || !src_image->data || !navla || !navlo) {
        LOG_ERROR("Parámetros inválidos para reproject_image_to_geographics.");
        return image_create(0, 0, 0);
    }

    float target_lon_min, target_lon_max, target_lat_min, target_lat_max;
    if (clip_coords) {
        target_lon_min = (clip_coords[0] > navlo->fmin) ? clip_coords[0] : navlo->fmin;
        target_lon_max = (clip_coords[2] < navlo->fmax) ? clip_coords[2] : navlo->fmax;
        target_lat_min = (clip_coords[3] > navla->fmin) ? clip_coords[3] : navla->fmin;
        target_lat_max = (clip_coords[1] < navla->fmax) ? clip_coords[1] : navla->fmax;
    } else {
        target_lon_min = navlo->fmin;
        target_lon_max = navlo->fmax;
        target_lat_min = navla->fmin;
        target_lat_max = navla->fmax;
    }

    float lon_range = target_lon_max - target_lon_min;
    float lat_range = target_lat_max - target_lat_min;
    float lat_center = (target_lat_min + target_lat_max) / 2.0f;
    float lat_rad = lat_center * (M_PI / 180.0f);
    float km_per_deg_lat = 111.132954f - 0.559822f * cosf(2.0f * lat_rad);
    float target_res_km = (native_resolution_km > 0.0f) ? native_resolution_km : 1.0f;
    float target_res_deg = target_res_km / km_per_deg_lat;

    size_t width = (size_t)(lon_range / target_res_deg + 0.5f);
    size_t height = (size_t)(lat_range / target_res_deg + 0.5f);

    if (width < 10) width = 10;
    if (height < 10) height = 10;

    const size_t MAX_DIM = 10000;
    if (width > MAX_DIM) width = MAX_DIM;
    if (height > MAX_DIM) height = MAX_DIM;

    LOG_INFO("Reproyectando imagen: %ux%u (bpp:%u) -> %zux%zu", 
             src_image->width, src_image->height, src_image->bpp, width, height);

    ImageData geo_image = image_create(width, height, src_image->bpp);
    if (!geo_image.data) {
        LOG_FATAL("Falla de memoria al crear la imagen geográfica de destino.");
        return geo_image;
    }
    // Inicializar a negro/transparente
    memset(geo_image.data, 0, geo_image.width * geo_image.height * geo_image.bpp);

    float lon_scale = (width > 1) ? (width - 1) / lon_range : 0;
    float lat_scale = (height > 1) ? (height - 1) / lat_range : 0;

    #pragma omp parallel for collapse(2)
    for (unsigned y = 0; y < src_image->height; y++) {
        for (unsigned x = 0; x < src_image->width; x++) {
            size_t src_idx = y * src_image->width + x;
            float lo = navlo->data_in[src_idx];
            float la = navla->data_in[src_idx];

            if (lo != NonData && la != NonData) {
                int ix = (int)((lo - target_lon_min) * lon_scale);
                int iy = (int)((target_lat_max - la) * lat_scale);

                if (ix >= 0 && ix < (int)width && iy >= 0 && iy < (int)height) {
                    size_t dst_idx = (size_t)iy * width + (size_t)ix;
                    
                    // Copiar el píxel (puede ser 1, 2, 3 o 4 bytes)
                    // Usamos un bucle para ser genéricos con el bpp
                    for (unsigned int c = 0; c < src_image->bpp; c++) {
                        #pragma omp atomic write
                        geo_image.data[dst_idx * src_image->bpp + c] = src_image->data[src_idx * src_image->bpp + c];
                    }
                }
            }
        }
    }

    LOG_INFO("Iniciando relleno de huecos (interpolación de vecinos)...");
    
    // Usar 8 vecinos para mejor cobertura y múltiples iteraciones
    const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const int num_neighbors = 8;
    const int max_iterations = 5; // Iteraciones para cerrar huecos más grandes
    
    // Crear buffer auxiliar para el algoritmo iterativo
    ImageData current = geo_image; // Mantener referencia al original
    ImageData next = image_create(width, height, src_image->bpp);
    
    if (next.data == NULL) {
        LOG_FATAL("Falla de memoria al crear buffer de relleno.");
        return current;
    }

    for (int iter = 0; iter < max_iterations; iter++) {
        int filled_count = 0;
        
        #pragma omp parallel for collapse(2) reduction(+:filled_count)
        for (size_t y = 0; y < height; y++) {
            for (size_t x = 0; x < width; x++) {
                size_t idx = y * width + x;
                size_t byte_idx = idx * src_image->bpp;
                
                // Verificar si el píxel ya tiene datos (si el canal alpha > 0 o si bpp==1 y valor > 0)
                bool has_data = false;
                if (src_image->bpp == 2 || src_image->bpp == 4) {
                    // Tiene canal alpha (el último canal)
                    has_data = (current.data[byte_idx + src_image->bpp - 1] > 0);
                } else {
                    // Sin alpha, verificar si algún canal tiene valor
                    for (unsigned int c = 0; c < src_image->bpp; c++) {
                        if (current.data[byte_idx + c] > 0) {
                            has_data = true;
                            break;
                        }
                    }
                }
                
                if (has_data) {
                    // Si ya tiene valor, lo conservamos
                    for (unsigned int c = 0; c < src_image->bpp; c++) {
                        next.data[byte_idx + c] = current.data[byte_idx + c];
                    }
                } else {
                    // Si no tiene datos, intentamos rellenar con vecinos
                    int sum[4] = {0, 0, 0, 0}; // Hasta 4 canales
                    int count = 0;
                    
                    for (int k = 0; k < num_neighbors; k++) {
                        int nx = (int)x + dx[k];
                        int ny = (int)y + dy[k];
                        
                        if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                            size_t n_idx = (size_t)ny * width + (size_t)nx;
                            size_t n_byte_idx = n_idx * src_image->bpp;
                            
                            // Verificar si el vecino tiene datos
                            bool neighbor_has_data = false;
                            if (src_image->bpp == 2 || src_image->bpp == 4) {
                                neighbor_has_data = (current.data[n_byte_idx + src_image->bpp - 1] > 0);
                            } else {
                                for (unsigned int c = 0; c < src_image->bpp; c++) {
                                    if (current.data[n_byte_idx + c] > 0) {
                                        neighbor_has_data = true;
                                        break;
                                    }
                                }
                            }
                            
                            if (neighbor_has_data) {
                                for (unsigned int c = 0; c < src_image->bpp; c++) {
                                    sum[c] += current.data[n_byte_idx + c];
                                }
                                count++;
                            }
                        }
                    }
                    
                    if (count > 0) {
                        for (unsigned int c = 0; c < src_image->bpp; c++) {
                            next.data[byte_idx + c] = (uint8_t)(sum[c] / count);
                        }
                        filled_count++;
                    } else {
                        // Sigue sin datos
                        for (unsigned int c = 0; c < src_image->bpp; c++) {
                            next.data[byte_idx + c] = 0;
                        }
                    }
                }
            }
        }
        
        LOG_DEBUG("Iteración %d: %d píxeles rellenados.", iter + 1, filled_count);
        
        if (filled_count == 0) {
            break;
        }
        
        // Swap de punteros para la siguiente iteración
        uint8_t* temp_ptr = current.data;
        current.data = next.data;
        next.data = temp_ptr;
    }
    
    LOG_INFO("Relleno de huecos terminado.");

    // Liberar memoria auxiliar (next tiene el buffer viejo tras el último swap o el inicial)
    image_destroy(&next);

    // Devolvemos la imagen rellena (current tiene el resultado final)
    return current;
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