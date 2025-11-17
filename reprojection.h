/*
 * Geostationary to Geographics Reprojection Module
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_REPROJECTION_H_
#define HPSATVIEWS_REPROJECTION_H_

#include "datanc.h"

DataF reproject_to_geographics(const DataF* source_data, const char* nav_reference_file,
                               float* out_lon_min, float* out_lon_max,
                               float* out_lat_min, float* out_lat_max);

DataF reproject_to_geographics_with_nav(const DataF* source_data, const DataF* navla, const DataF* navlo,
                                        float* out_lon_min, float* out_lon_max,
                                        float* out_lat_min, float* out_lat_max);

/**
 * @brief Encuentra el píxel más cercano a una coordenada geográfica dada en una malla no reproyectada.
 *
 * Esta función busca en las mallas de navegación de latitud y longitud para encontrar el índice
 * del píxel (columna, fila) que está geográficamente más cerca de las coordenadas de destino.
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
                                       int* out_ix, int* out_iy);

#endif /* HPSATVIEWS_REPROJECTION_H_ */