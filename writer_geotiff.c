#define _GNU_SOURCE
/*
 * GeoTIFF writer module
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "writer_geotiff.h"
#include "projection_utils.h"
#include "logger.h"
#include <gdal.h>
#include <cpl_conv.h>
#include <stdlib.h>
#include <string.h>

// Helper function to set up georeferencing and create the dataset
static GDALDatasetH create_georeferenced_dataset(const char* filename, int width, int height, int bands,
                                                 GDALDataType eDataType, const DataF* navla, const DataF* navlo,
                                                 const char* nc_reference_file, bool is_geographic,
                                                 unsigned crop_x_start, unsigned crop_y_start,
                                                 int offset_x_pixels, int offset_y_pixels) {
    char* wkt = NULL;
    double geotransform[6];

    if (is_geographic) {
        wkt = strdup("EPSG:4326");
        compute_geotransform_geographic(navla, navlo, geotransform);
    } else {
        wkt = build_geostationary_wkt_from_nc(nc_reference_file);
        
        // Para geoestacionario: si tenemos navegación, calcular desde las coordenadas reales
        // En lugar de usar índices del NetCDF que pueden no corresponder al crop fino
        if (navla && navlo && navla->width == (size_t)width && navla->height == (size_t)height) {
            // Calcular GeoTransform desde las coordenadas lat/lon de la navegación
            // usando la relación entre lat/lon y las coordenadas geoestacionarias
            LOG_INFO("Calculando GeoTransform desde navegación: lat[%.3f,%.3f], lon[%.3f,%.3f]",
                     navla->fmin, navla->fmax, navlo->fmin, navlo->fmax);
            
            // Para geoestacionario necesitamos transformar lat/lon a x/y en metros
            // Usaremos el mismo pixel size que GDAL calcula
            double lat_center = (navla->fmin + navla->fmax) / 2.0;
            double lon_center = (navlo->fmin + navlo->fmax) / 2.0;
            
            LOG_INFO("Centro navegación: lat=%.3f, lon=%.3f", lat_center, lon_center);
            
            // Por ahora, usar el método con índices como fallback
            // TODO: implementar transformación lat/lon -> geos correctamente
            if (compute_geotransform_geostationary(nc_reference_file, width, height, crop_x_start, crop_y_start,
                                                   offset_x_pixels, offset_y_pixels, geotransform) != 0) {
                free(wkt);
                return NULL;
            }
        } else {
            if (compute_geotransform_geostationary(nc_reference_file, width, height, crop_x_start, crop_y_start,
                                                   offset_x_pixels, offset_y_pixels, geotransform) != 0) {
                free(wkt);
                return NULL;
            }
        }
    }

    if (!wkt) {
        LOG_ERROR("Error al generar WKT de proyección");
        return NULL;
    }

    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        LOG_ERROR("No se encontró el driver GTiff. Asegúrese que GDAL está bien instalado.");
        free(wkt);
        return NULL;
    }
    
    GDALDatasetH dataset = GDALCreate(driver, filename, width, height, bands, eDataType, NULL);
    if (!dataset) {
        LOG_ERROR("Error al crear dataset GeoTIFF: %s", filename);
        free(wkt);
        return NULL;
    }

    GDALSetProjection(dataset, wkt);
    GDALSetGeoTransform(dataset, geotransform);

    free(wkt);
    return dataset;
}

int write_geotiff_rgb(const char* filename, const ImageData* img, const DataF* navla,
                     const DataF* navlo, const char* nc_reference_file, bool is_geographic,
                     unsigned crop_x_start, unsigned crop_y_start,
                     int offset_x_pixels, int offset_y_pixels) {
    if (!filename || !img || !img->data || img->bpp != 3) {
        LOG_ERROR("Parámetros inválidos para write_geotiff_rgb");
        return -1;
    }

    GDALDatasetH dataset = create_georeferenced_dataset(filename, img->width, img->height, 3, GDT_Byte,
                                                        navla, navlo, nc_reference_file, is_geographic,
                                                        crop_x_start, crop_y_start,
                                                        offset_x_pixels, offset_y_pixels);
    if (!dataset) return -1;

    size_t channel_size = img->width * img->height;
    unsigned char* channel_data = malloc(channel_size);
    if (!channel_data) {
        LOG_ERROR("Error de memoria para buffer de canal RGB");
        GDALClose(dataset);
        return -1;
    }

    CPLErr io_err = CE_None;
    for (int band = 1; band <= 3; band++) {
        GDALRasterBandH band_h = GDALGetRasterBand(dataset, band);
        for (size_t i = 0; i < channel_size; i++) {
            channel_data[i] = img->data[i * 3 + (band - 1)];
        }
        io_err = GDALRasterIO(band_h, GF_Write, 0, 0, img->width, img->height,
                              channel_data, img->width, img->height, GDT_Byte, 0, 0);
        if (io_err != CE_None) {
            LOG_ERROR("Error al escribir la banda %d del GeoTIFF.", band);
            break;
        }
    }

    free(channel_data);
    GDALClose(dataset);

    if (io_err == CE_None) {
        LOG_INFO("GeoTIFF RGB guardado: %s (%ux%u, proyección: %s)", filename, img->width, img->height,
                 is_geographic ? "geográfica" : "geoestacionaria");
    }

    return (io_err == CE_None) ? 0 : -1;
}

int write_geotiff_gray(const char* filename, const ImageData* img, const DataF* navla,
                      const DataF* navlo, const char* nc_reference_file, bool is_geographic,
                      unsigned crop_x_start, unsigned crop_y_start,
                      int offset_x_pixels, int offset_y_pixels) {
    if (!filename || !img || !img->data || img->bpp != 1) {
        LOG_ERROR("Parámetros inválidos para write_geotiff_gray");
        return -1;
    }

    GDALDatasetH dataset = create_georeferenced_dataset(filename, img->width, img->height, 1, GDT_Byte,
                                                        navla, navlo, nc_reference_file, is_geographic,
                                                        crop_x_start, crop_y_start,
                                                        offset_x_pixels, offset_y_pixels);
    if (!dataset) return -1;

    GDALRasterBandH band_h = GDALGetRasterBand(dataset, 1);
    CPLErr io_err = GDALRasterIO(band_h, GF_Write, 0, 0, img->width, img->height,
                                 img->data, img->width, img->height, GDT_Byte, 0, 0);

    GDALClose(dataset);

    if (io_err == CE_None) {
        LOG_INFO("GeoTIFF en escala de grises guardado: %s (%ux%u, proyección: %s)", filename, img->width, img->height,
                 is_geographic ? "geográfica" : "geoestacionaria");
    } else {
        LOG_ERROR("Error al escribir datos del GeoTIFF.");
    }
    
    return (io_err == CE_None) ? 0 : -1;
}

int write_geotiff_indexed(const char* filename, const ImageData* img, const ColorArray* palette,
                          const DataF* navla, const DataF* navlo, const char* nc_reference_file,
                          bool is_geographic, unsigned crop_x_start, unsigned crop_y_start,
                          int offset_x_pixels, int offset_y_pixels) {
    if (!filename || !img || !img->data || img->bpp != 1 || !palette) {
        LOG_ERROR("Parámetros inválidos para write_geotiff_indexed");
        return -1;
    }

    GDALDatasetH dataset = create_georeferenced_dataset(filename, img->width, img->height, 1, GDT_Byte,
                                                        navla, navlo, nc_reference_file, is_geographic,
                                                        crop_x_start, crop_y_start,
                                                        offset_x_pixels, offset_y_pixels);
    if (!dataset) return -1;

    GDALRasterBandH band_h = GDALGetRasterBand(dataset, 1);

    GDALColorTableH color_table = GDALCreateColorTable(GPI_RGB);
    for (unsigned i = 0; i < palette->length; i++) {
        GDALColorEntry entry = {palette->colors[i].r, palette->colors[i].g, palette->colors[i].b, 255};
        GDALSetColorEntry(color_table, i, &entry);
    }
    GDALSetRasterColorTable(band_h, color_table);
    GDALDestroyColorTable(color_table);

    GDALSetRasterColorInterpretation(band_h, GCI_PaletteIndex);

    CPLErr io_err = GDALRasterIO(band_h, GF_Write, 0, 0, img->width, img->height,
                                 img->data, img->width, img->height, GDT_Byte, 0, 0);

    GDALClose(dataset);

    if (io_err == CE_None) {
        LOG_INFO("GeoTIFF indexado guardado: %s (%ux%u, proyección: %s)", filename, img->width, img->height,
                 is_geographic ? "geográfica" : "geoestacionaria");
    } else {
        LOG_ERROR("Error al escribir datos del GeoTIFF indexado.");
    }

    return (io_err == CE_None) ? 0 : -1;
}