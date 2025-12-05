/*
 * GeoTIFF writer module
 * Copyright (c) 2025 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_WRITER_GEOTIFF_H_
#define HPSATVIEWS_WRITER_GEOTIFF_H_

#include "image.h"
#include "datanc.h"
#include "reader_cpt.h" // Asumo que aquí defines ColorArray

/**
 * @brief Escribe una imagen RGB (3 canales) a formato GeoTIFF.
 * * @param filename Ruta del archivo de salida (.tif).
 * @param img Puntero a la imagen RGB (bpp=3).
 * @param meta Metadatos originales (contienen proyección y geotransform base).
 * @param offset_x Desplazamiento en X (píxeles) si la imagen es un recorte (crop).
 * @param offset_y Desplazamiento en Y (píxeles) si la imagen es un recorte (crop).
 * @return 0 en éxito, -1 en error.
 */
int write_geotiff_rgb(const char* filename,
                      const ImageData* img,
                      const DataNC* meta,
                      int offset_x,
                      int offset_y);

/**
 * @brief Escribe una imagen en escala de grises (1 canal) a formato GeoTIFF.
 * * @param filename Ruta del archivo de salida.
 * @param img Puntero a la imagen (bpp=1).
 * @param meta Metadatos originales.
 * @param offset_x Desplazamiento en X para recortes.
 * @param offset_y Desplazamiento en Y para recortes.
 * @return 0 en éxito, -1 en error.
 */
int write_geotiff_gray(const char* filename,
                       const ImageData* img,
                       const DataNC* meta,
                       int offset_x,
                       int offset_y);

/**
 * @brief Escribe una imagen indexada (Paleta de colores) a formato GeoTIFF.
 * * @param filename Ruta del archivo de salida.
 * @param img Puntero a la imagen indexada (bpp=1, valores 0-255).
 * @param palette Paleta de colores a incrustar en el TIFF.
 * @param meta Metadatos originales.
 * @param offset_x Desplazamiento en X para recortes.
 * @param offset_y Desplazamiento en Y para recortes.
 * @return 0 en éxito, -1 en error.
 */
int write_geotiff_indexed(const char* filename,
                          const ImageData* img,
                          const ColorArray* palette,
                          const DataNC* meta,
                          int offset_x,
                          int offset_y);

#endif /* HPSATVIEWS_WRITER_GEOTIFF_H_ */
