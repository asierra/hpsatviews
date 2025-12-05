/*
 * Projection utilities for GeoTIFF georeferencing
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "projection_utils.h"
#include "reader_nc.h"
#include "logger.h"
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ERR(e) { LOG_ERROR("NetCDF error: %s", nc_strerror(e)); return NULL; }
#define ERR_INT(e) { LOG_ERROR("NetCDF error: %s", nc_strerror(e)); return -1; }

char* build_geostationary_wkt_from_nc(const char* nc_filename) {
    int ncid, varid, retval;
    
    if ((retval = nc_open(nc_filename, NC_NOWRITE, &ncid)))
        ERR(retval);
    
    // Leer metadatos de proyección
    if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid))) {
        nc_close(ncid);
        ERR(retval);
    }
    
    float perspective_height, lon_origin;
    
    if ((retval = nc_get_att_float(ncid, varid, "perspective_point_height", &perspective_height))) {
        nc_close(ncid);
        ERR(retval);
    }
    if ((retval = nc_get_att_float(ncid, varid, "longitude_of_projection_origin", &lon_origin))) {
        nc_close(ncid);
        ERR(retval);
    }
    
    nc_close(ncid);
    
    char* wkt = malloc(512);
    if (!wkt) {
        LOG_ERROR("Error de asignación de memoria para WKT");
        return NULL;
    }
    
    // Usar formato PROJ (más robusto y compatible con GDAL moderno)
    // sweep=x es el estándar para GOES-R
    snprintf(wkt, 512,
        "+proj=geos +lon_0=%.6f +h=%.1f +x_0=0 +y_0=0 +ellps=GRS80 +units=m +no_defs +sweep=x",
        lon_origin,
        perspective_height);
    
    LOG_DEBUG("PROJ string geoestacionario: %s", wkt);
    return wkt;
}

void compute_geotransform_geographic(const DataF* navla, const DataF* navlo,
                                     double geotransform[6]) {
    // GeoTransform para GDAL usa "pixel-as-area" convention:
    // La coordenada del píxel se refiere a la ESQUINA SUPERIOR IZQUIERDA
    // No al centro del píxel
    
    // La navegación usa grid-registration (centros en fmin/fmax)
    // Calcular el tamaño del píxel desde los centros
    double pixel_width = (navlo->fmax - navlo->fmin) / (navlo->width - 1);
    double pixel_height = (navla->fmax - navla->fmin) / (navla->height - 1);
    
    // GeoTransform[0] = esquina superior izquierda X (NOT centro del primer píxel)
    // GeoTransform[3] = esquina superior izquierda Y (NOT centro del primer píxel)
    geotransform[0] = navlo->fmin - pixel_width / 2.0;   // Ajustar al borde
    geotransform[3] = navla->fmax + pixel_height / 2.0;  // Ajustar al borde
    geotransform[1] = pixel_width;
    geotransform[5] = -pixel_height;  // Negativo porque Y decrece hacia abajo
    geotransform[2] = 0.0;
    geotransform[4] = 0.0;
    
    LOG_INFO("GeoTransform geográfico: [%.6f, %.9f, 0, %.6f, 0, %.9f]",
              geotransform[0], geotransform[1], geotransform[3], geotransform[5]);
}

int compute_geotransform_geostationary(const char* nc_filename,
                                       unsigned width, unsigned height,
                                       unsigned crop_x_start, unsigned crop_y_start,
                                       int offset_x_pixels, int offset_y_pixels,
                                       double geotransform[6]) {
    int ncid, xid, yid, varid, retval;
    
    if ((retval = nc_open(nc_filename, NC_NOWRITE, &ncid)))
        ERR_INT(retval);
    
    // Leer parámetros de proyección
    if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid))) {
        nc_close(ncid);
        ERR_INT(retval);
    }
    
    float perspective_height, semi_major;
    if ((retval = nc_get_att_float(ncid, varid, "perspective_point_height", &perspective_height))) {
        nc_close(ncid);
        ERR_INT(retval);
    }
    if ((retval = nc_get_att_float(ncid, varid, "semi_major_axis", &semi_major))) {
        nc_close(ncid);
        ERR_INT(retval);
    }
    
    double r_eq = semi_major + perspective_height;
    
    // Leer las variables x[] e y[] del NetCDF (contienen coordenadas en radianes)
    int x_varid, y_varid;
    if ((retval = nc_inq_varid(ncid, "x", &x_varid))) {
        nc_close(ncid);
        ERR_INT(retval);
    }
    if ((retval = nc_inq_varid(ncid, "y", &y_varid))) {
        nc_close(ncid);
        ERR_INT(retval);
    }
    
    // Leer los valores de x[] e y[] para el subset que necesitamos
    // Estas variables están en formato packed (short) con scale_factor y add_offset
    short* x_vals_raw = (short*)malloc(width * sizeof(short));
    short* y_vals_raw = (short*)malloc(height * sizeof(short));
    if (!x_vals_raw || !y_vals_raw) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        LOG_ERROR("Error de memoria en compute_geotransform_geostationary");
        return -1;
    }
    
    // Leer scale_factor y add_offset
    float x_sf, y_sf, x_ao, y_ao;
    if ((retval = nc_get_att_float(ncid, x_varid, "scale_factor", &x_sf))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR_INT(retval);
    }
    if ((retval = nc_get_att_float(ncid, x_varid, "add_offset", &x_ao))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR_INT(retval);
    }
    if ((retval = nc_get_att_float(ncid, y_varid, "scale_factor", &y_sf))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR_INT(retval);
    }
    if ((retval = nc_get_att_float(ncid, y_varid, "add_offset", &y_ao))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR_INT(retval);
    }
    
    size_t start_x = crop_x_start;
    size_t count_x = width;
    size_t start_y = crop_y_start;
    size_t count_y = height;
    
    if ((retval = nc_get_vara_short(ncid, x_varid, &start_x, &count_x, x_vals_raw))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR_INT(retval);
    }
    if ((retval = nc_get_vara_short(ncid, y_varid, &start_y, &count_y, y_vals_raw))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR_INT(retval);
    }
    
    nc_close(ncid);
    
    // Aplicar scale_factor y add_offset para obtener coordenadas en radianes
    // x[0] es el oeste, x[n-1] es el este
    // y[0] es el norte, y[n-1] es el sur
    double x_min_rad = (double)x_vals_raw[0] * x_sf + x_ao;
    double x_max_rad = (double)x_vals_raw[width - 1] * x_sf + x_ao;
    double y_max_rad = (double)y_vals_raw[0] * y_sf + y_ao;        // y[0] es el norte
    double y_min_rad = (double)y_vals_raw[height - 1] * y_sf + y_ao;  // y[n-1] es el sur
    
    LOG_INFO("Coordenadas NetCDF crop[%u,%u] size[%u,%u]: x_raw[%d→%d], y_raw[%d→%d]",
             crop_x_start, crop_y_start, width, height,
             x_vals_raw[0], x_vals_raw[width-1], y_vals_raw[0], y_vals_raw[height-1]);
    LOG_INFO("Coordenadas en radianes: x[%.6f→%.6f], y[%.6f→%.6f]",
             x_min_rad, x_max_rad, y_min_rad, y_max_rad);
    
    free(x_vals_raw);
    free(y_vals_raw);
    
    // Convertir de radianes a metros
    double x_min = x_min_rad * r_eq;
    double x_max = x_max_rad * r_eq;
    double y_min = y_min_rad * r_eq;
    double y_max = y_max_rad * r_eq;
    
    // Calcular tamaño del píxel (usando coordenadas de los centros)
    double pixel_size_x = (x_max - x_min) / (double)(width - 1);
    double pixel_size_y = (y_max - y_min) / (double)(height - 1);
    
    // Ajustar a la ESQUINA del primer píxel (pixel-as-area convention)
    double origin_x = x_min - pixel_size_x / 2.0;
    double origin_y = y_max + pixel_size_y / 2.0;
    
    LOG_INFO("Origin ANTES del offset: (%.2f, %.2f) m", origin_x, origin_y);
    
    // Aplicar offset si se proporciona
    if (offset_x_pixels != 0 || offset_y_pixels != 0) {
        double offset_x_meters = offset_x_pixels * pixel_size_x;
        double offset_y_meters = offset_y_pixels * pixel_size_y;
        
        origin_x += offset_x_meters;
        origin_y -= offset_y_meters;  // Y negativo porque pixel_size_y es negativo
        
        LOG_INFO("Aplicando offset: [%d,%d] píxeles = [%.2f, %.2f] m", 
                 offset_x_pixels, offset_y_pixels, offset_x_meters, offset_y_meters);
        LOG_INFO("Origin DESPUÉS del offset: (%.2f, %.2f) m", origin_x, origin_y);
    }
    
    geotransform[0] = origin_x;
    geotransform[3] = origin_y;
    geotransform[1] = pixel_size_x;
    geotransform[5] = -pixel_size_y;
    geotransform[2] = 0.0;
    geotransform[4] = 0.0;
    
    LOG_INFO("GeoTransform geoestacionario (crop[%u,%u], size[%u,%u]): origin=(%.2f, %.2f) m, pixel_size=(%.2f, %.2f) m",
              crop_x_start, crop_y_start, width, height, geotransform[0], geotransform[3], geotransform[1], geotransform[5]);
    
    return 0;
}