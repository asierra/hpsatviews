/*
 * Projection utilities for GeoTIFF georeferencing
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_PROJECTION_UTILS_H_
#define HPSATVIEWS_PROJECTION_UTILS_H_

#include "datanc.h"
#include <stdbool.h>

/**
 * @brief Construye una cadena WKT para proyección geoestacionaria desde NetCDF.
 * 
 * Lee los metadatos de proyección del archivo NetCDF GOES y genera una cadena
 * WKT compatible con GDAL para la proyección geoestacionaria.
 * 
 * @param nc_filename Ruta al archivo NetCDF de referencia
 * @return Cadena WKT asignada dinámicamente (debe liberarse con free()),
 *         o NULL en caso de error
 */
char* build_geostationary_wkt_from_nc(const char* nc_filename);

/**
 * @brief Calcula el GeoTransform para proyección geográfica desde navegación.
 * 
 * GeoTransform[6] = {origen_x, pixel_width, 0, origen_y, 0, -pixel_height}
 * 
 * @param navla Malla de latitudes
 * @param navlo Malla de longitudes
 * @param geotransform Array de salida con 6 elementos
 */
void compute_geotransform_geographic(const DataF* navla, const DataF* navlo,
                                     double geotransform[6]);

/**
 * @brief Calcula el GeoTransform para proyección geoestacionaria desde NetCDF.
 * 
 * Lee los arrays x[] e y[] (en radianes) del archivo NetCDF y calcula el
 * GeoTransform correspondiente para la proyección nativa geoestacionaria.
 * 
 * @param nc_filename Ruta al archivo NetCDF de referencia
 * @param width Ancho de la imagen en píxeles
 * @param height Alto de la imagen en píxeles
 * @param crop_x_start Índice X de inicio del recorte en el NetCDF original (0 para imagen completa)
 * @param crop_y_start Índice Y de inicio del recorte en el NetCDF original (0 para imagen completa)
 * @param clip_bounds Límites geográficos del clip [lon_min, lat_max, lon_max, lat_min] o NULL
 * @param geotransform Array de salida con 6 elementos
 * @return 0 en éxito, código de error en fallo
 */
int compute_geotransform_geostationary(const char* nc_filename,
                                       unsigned width, unsigned height,
                                       unsigned crop_x_start, unsigned crop_y_start,
                                       int offset_x_pixels, int offset_y_pixels,
                                       double geotransform[6]);

#endif /* HPSATVIEWS_PROJECTION_UTILS_H_ */