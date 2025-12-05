/*
 * GeoTIFF writer module
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_WRITER_GEOTIFF_H_
#define HPSATVIEWS_WRITER_GEOTIFF_H_

#include "image.h"
#include "datanc.h"
#include "reader_cpt.h"

/**
 * @brief Escribe una imagen RGB a formato GeoTIFF georreferenciado.
 * 
 * @param filename Nombre del archivo de salida (.tif o .tiff)
 * @param img Imagen RGB (bpp=3) a escribir
 * @param navla Malla de latitudes (navegación)
 * @param navlo Malla de longitudes (navegación)
 * @param nc_reference_file Archivo NetCDF de referencia para metadatos de proyección
 * @param is_geographic true si es proyección geográfica (EPSG:4326), false para geoestacionaria
 * @return 0 en éxito, código de error en fallo
 */
int write_geotiff_rgb(const char* filename,
                     const ImageData* img,
                     const DataF* navla,
                     const DataF* navlo,
                     const char* nc_reference_file,
                     bool is_geographic,
                     unsigned crop_x_start,
                     unsigned crop_y_start,
                     int offset_x_pixels,
                     int offset_y_pixels);

/**
 * @brief Escribe una imagen en escala de grises a formato GeoTIFF.
 * 
 * Para imágenes singlegray sin paleta. Escribe datos como UInt8 escalados.
 * 
 * @param filename Nombre del archivo de salida
 * @param img Imagen en escala de grises (bpp=1)
 * @param navla Malla de latitudes
 * @param navlo Malla de longitudes
 * @param nc_reference_file Archivo NetCDF de referencia
 * @param is_geographic true si es proyección geográfica, false para geoestacionaria
 * @return 0 en éxito, código de error en fallo
 */
int write_geotiff_gray(const char* filename,
                      const ImageData* img,
                      const DataF* navla,
                      const DataF* navlo,
                      const char* nc_reference_file,
                      bool is_geographic,
                      unsigned crop_x_start,
                      unsigned crop_y_start,
                      int offset_x_pixels,
                      int offset_y_pixels);

/**
 * @brief Escribe una imagen indexada con paleta CPT a formato GeoTIFF.
 * 
 * Para imágenes pseudocolor. Escribe 1 banda indexada + Color Table GDAL.
 * 
 * @param filename Nombre del archivo de salida
 * @param img Imagen indexada (bpp=1, valores 0-N)
 * @param palette Paleta de colores dinámica (ColorArray)
 * @param navla Malla de latitudes
 * @param navlo Malla de longitudes
 * @param nc_reference_file Archivo NetCDF de referencia
 * @param is_geographic true si es proyección geográfica, false para geoestacionaria
 * @return 0 en éxito, código de error en fallo
 */
int write_geotiff_indexed(const char* filename,
                         const ImageData* img,
                         const ColorArray* palette,
                         const DataF* navla,
                         const DataF* navlo,
                         const char* nc_reference_file,
                         bool is_geographic,
                         unsigned crop_x_start,
                         unsigned crop_y_start,
                         int offset_x_pixels,
                         int offset_y_pixels);

#endif /* HPSATVIEWS_WRITER_GEOTIFF_H_ */