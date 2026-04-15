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
                // Manejar cruce del antimeridiano
                if (lon_diff > 180.0f) lon_diff -= 360.0f;
                else if (lon_diff < -180.0f) lon_diff += 360.0f;
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

ImageData reproject_image_analytical(const ImageData* src_image, const DataNC* data_nc,
                                     float lat_min, float lat_max,
                                     float lon_min, float lon_max,
                                     float native_resolution_km,
                                     const float* clip_coords) {
    if (!src_image || !src_image->data || !data_nc || !data_nc->proj_info.valid) {
        LOG_ERROR("Parámetros inválidos para reproject_image_analytical.");
        return image_create(0, 0, 0);
    }

    // Parámetros de proyección
    double a   = data_nc->proj_info.semi_major;
    double b   = data_nc->proj_info.semi_minor;
    double H   = a + data_nc->proj_info.sat_height;
    double e2  = (a * a - b * b) / (a * a);
    double a2  = a * a;
    double b2  = b * b;
    double a2_over_b2 = a2 / b2;
    double lambda0 = data_nc->proj_info.lon_origin * (M_PI / 180.0);

    const double *gt = data_nc->geotransform;
    unsigned int src_w = data_nc->fdata.width > 0 ? data_nc->fdata.width :
                         (data_nc->bdata.width > 0 ? data_nc->bdata.width : src_image->width);
    unsigned int src_h = data_nc->fdata.height > 0 ? data_nc->fdata.height :
                         (data_nc->bdata.height > 0 ? data_nc->bdata.height : src_image->height);

    // Extensión geográfica destino
    float target_lon_min, target_lon_max, target_lat_min, target_lat_max;
    if (clip_coords) {
        target_lon_min = (clip_coords[0] > lon_min) ? clip_coords[0] : lon_min;
        target_lon_max = (clip_coords[2] < lon_max) ? clip_coords[2] : lon_max;
        target_lat_min = (clip_coords[3] > lat_min) ? clip_coords[3] : lat_min;
        target_lat_max = (clip_coords[1] < lat_max) ? clip_coords[1] : lat_max;
    } else {
        target_lon_min = lon_min;
        target_lon_max = lon_max;
        target_lat_min = lat_min;
        target_lat_max = lat_max;
    }

    float lon_range = target_lon_max - target_lon_min;
    float lat_range = target_lat_max - target_lat_min;
    float lat_center = (target_lat_min + target_lat_max) / 2.0f;
    float lat_rad_c = lat_center * (float)(M_PI / 180.0);
    float km_per_deg_lat = 111.132954f - 0.559822f * cosf(2.0f * lat_rad_c);
    float target_res_km = (native_resolution_km > 0.0f) ? native_resolution_km : 1.0f;
    float target_res_deg = target_res_km / km_per_deg_lat;

    size_t width  = (size_t)(lon_range / target_res_deg + 0.5f);
    size_t height = (size_t)(lat_range / target_res_deg + 0.5f);

    if (width  < 10) width  = 10;
    if (height < 10) height = 10;

    const size_t MAX_DIM = 10000;
    if (width  > MAX_DIM) width  = MAX_DIM;
    if (height > MAX_DIM) height = MAX_DIM;

    LOG_INFO("Reproyección analítica: %ux%u (bpp:%u) -> %zux%zu",
             src_image->width, src_image->height, src_image->bpp, width, height);

    ImageData geo_image = image_create(width, height, src_image->bpp);
    if (!geo_image.data) {
        LOG_FATAL("Falla de memoria al crear la imagen geográfica de destino.");
        return geo_image;
    }
    memset(geo_image.data, 0, width * height * src_image->bpp);

    double t_start = omp_get_wtime();
    unsigned int bpp = src_image->bpp;

    // Resolución por píxel de salida
    double deg_per_px_lon = (double)lon_range / (double)width;
    double deg_per_px_lat = (double)lat_range / (double)height;

    #pragma omp parallel for collapse(2)
    for (size_t oy = 0; oy < height; oy++) {
        for (size_t ox = 0; ox < width; ox++) {
            // Coordenada geográfica del centro del píxel destino (en grados)
            double lon_deg = (double)target_lon_min + ((double)ox + 0.5) * deg_per_px_lon;
            double lat_deg = (double)target_lat_max - ((double)oy + 0.5) * deg_per_px_lat;

            // Convertir a radianes
            double phi    = lat_deg * (M_PI / 180.0);
            double lambda = lon_deg * (M_PI / 180.0);

            // Latitud geocéntrica
            double phi_c = atan((b2 / a2) * tan(phi));
            double cos_phi_c = cos(phi_c);
            double sin_phi_c = sin(phi_c);

            // Radio geocéntrico
            double r_c = b / sqrt(1.0 - e2 * cos_phi_c * cos_phi_c);

            // Vector de posición del punto en la Tierra visto desde el satélite
            double d_lambda = lambda - lambda0;
            double cos_dl   = cos(d_lambda);
            double sin_dl   = sin(d_lambda);

            double s_x = H - r_c * cos_phi_c * cos_dl;
            double s_y = -r_c * cos_phi_c * sin_dl;
            double s_z = r_c * sin_phi_c;

            // Verificación de visibilidad desde el satélite
            if (H * (H - s_x) < s_y * s_y + a2_over_b2 * s_z * s_z) {
                continue; // El punto no es visible — píxel queda negro
            }

            // Ángulos de escaneo (GOES-R PUG)
            double s_n = sqrt(s_x * s_x + s_y * s_y + s_z * s_z);
            double x_rad = asin(-s_y / s_n);
            double y_rad = atan2(s_z, s_x);

            // Convertir ángulos de escaneo a coordenadas píxel fuente
            double col = (x_rad - gt[0]) / gt[1];
            double row = (y_rad - gt[3]) / gt[5];

            // Verificar límites (con margen de 1 píxel para bilineal)
            if (col < 0.0 || col >= (double)(src_w - 1) ||
                row < 0.0 || row >= (double)(src_h - 1)) {
                continue;
            }

            // Interpolación bilineal
            int c0 = (int)col;
            int r0 = (int)row;
            double dc = col - c0;
            double dr = row - r0;

            int c1 = c0 + 1;
            int r1 = r0 + 1;

            double w00 = (1.0 - dc) * (1.0 - dr);
            double w10 = dc * (1.0 - dr);
            double w01 = (1.0 - dc) * dr;
            double w11 = dc * dr;

            size_t i00 = ((size_t)r0 * src_w + (size_t)c0) * bpp;
            size_t i10 = ((size_t)r0 * src_w + (size_t)c1) * bpp;
            size_t i01 = ((size_t)r1 * src_w + (size_t)c0) * bpp;
            size_t i11 = ((size_t)r1 * src_w + (size_t)c1) * bpp;

            size_t dst_idx = (oy * width + ox) * bpp;

            for (unsigned int ch = 0; ch < bpp; ch++) {
                double val = w00 * src_image->data[i00 + ch]
                           + w10 * src_image->data[i10 + ch]
                           + w01 * src_image->data[i01 + ch]
                           + w11 * src_image->data[i11 + ch];
                int ival = (int)(val + 0.5);
                geo_image.data[dst_idx + ch] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
            }
        }
    }

    double elapsed = omp_get_wtime() - t_start;
    LOG_TIMING(elapsed, "Reproyección analítica");

    return geo_image;
}

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