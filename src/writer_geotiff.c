/*
 * GeoTIFF writer module implementation
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "writer_geotiff.h"
#include "logger.h"
#include <gdal.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// --- Funciones Auxiliares Privadas ---

/**
 * Escribe metadatos del satélite/sector/banda en el dataset GDAL.
 * Se almacenan como items XML dentro del TIFF (dominio por defecto).
 */
static void set_gdal_metadata(GDALDatasetH ds, const DataNC *meta) {
    if (!ds || !meta) return;

    static const char *sat_names[] = {
        [SAT_UNKNOWN] = "unknown",
        [SAT_GOES16]  = "G16",
        [SAT_GOES17]  = "G17",
        [SAT_GOES18]  = "G18",
        [SAT_GOES19]  = "G19",
    };
    static const char *sector_names[] = {
        [SECTOR_UNKNOWN] = "",
        [SECTOR_FD]      = "fd",
        [SECTOR_CONUS]   = "conus",
        [SECTOR_M1]      = "m1",
        [SECTOR_M2]      = "m2",
    };

    GDALSetMetadataItem(ds, "tool", "hpsatviews", "");

    if (meta->sat_id >= SAT_UNKNOWN && meta->sat_id <= SAT_GOES19)
        GDALSetMetadataItem(ds, "satellite", sat_names[meta->sat_id], "");

    if (meta->sector_id >= SECTOR_UNKNOWN && meta->sector_id <= SECTOR_M2 &&
            sector_names[meta->sector_id][0])
        GDALSetMetadataItem(ds, "sector", sector_names[meta->sector_id], "");

    if (meta->band_id > 0 && meta->band_id <= 16) {
        char band_str[8];
        snprintf(band_str, sizeof(band_str), "C%02d", meta->band_id);
        GDALSetMetadataItem(ds, "band", band_str, "");
    }

    if (meta->timestamp > 0) {
        struct tm *tm_info = gmtime(&meta->timestamp);
        if (tm_info) {
            char ts_iso[32];
            strftime(ts_iso, sizeof(ts_iso), "%Y-%m-%dT%H:%M:%SZ", tm_info);
            GDALSetMetadataItem(ds, "scan_time", ts_iso, "");

            // TIFF standard datetime tag: "YYYY:MM:DD HH:MM:SS"
            char ts_tiff[20];
            strftime(ts_tiff, sizeof(ts_tiff), "%Y:%m:%d %H:%M:%S", tm_info);
            GDALSetMetadataItem(ds, "TIFFTAG_DATETIME", ts_tiff, "");
        }
    }
}

/**
 * Genera el string WKT usando PROJ.4 para máxima compatibilidad.
 * Reemplaza a OSRSetGeostationary para evitar errores de compilación.
 */
static char* get_projection_wkt(const DataNC* meta) {
    OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);
    char *wkt = NULL;

    if (meta->proj_code == PROJ_GEOS && meta->proj_info.valid) {
        // Construimos la cadena PROJ.4 manualmente.
        // +sweep=x es CRUCIAL para GOES-R.
        char proj4[512];
        snprintf(proj4, sizeof(proj4), 
                 "+proj=geos +sweep=x +lon_0=%.6f +h=%.3f +x_0=0 +y_0=0 +ellps=GRS80 +units=m +no_defs",
                 meta->proj_info.lon_origin,
                 meta->proj_info.sat_height);
        
        if (OSRImportFromProj4(hSRS, proj4) != OGRERR_NONE) {
            LOG_ERROR("Error importando proyección PROJ.4: %s", proj4);
        }

    } else if (meta->proj_code == PROJ_LATLON) {
        // EPSG:4326 (Latitud/Longitud WGS84)
        OSRImportFromEPSG(hSRS, 4326);
    } else {
        OSRDestroySpatialReference(hSRS);
        return NULL;
    }

    OSRExportToWkt(hSRS, &wkt);
    OSRDestroySpatialReference(hSRS);
    return wkt;
}

/**
 * Crea un Dataset en memoria (MEM), configura GeoTransform y proyección.
 * NOTA IMPORTANTE: Si la proyección es GEOS, convierte el GeoTransform
 * de Radianes a Metros multiplicando por la altura del satélite.
 */
static GDALDatasetH create_mem_dataset(int width, 
                                       int height, 
                                       int bands, 
                                       GDALDataType type, 
                                       const DataNC* meta,
                                       int offset_x, 
                                       int offset_y) {
    
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("MEM");
    if (!driver) {
        LOG_ERROR("Driver MEM no disponible en GDAL.");
        return NULL;
    }

    GDALDatasetH ds = GDALCreate(driver, "", width, height, bands, type, NULL);
    if (!ds) {
        LOG_ERROR("No se pudo crear dataset en memoria.");
        return NULL;
    }

    if (meta) {
        // 1. Establecer Proyección (WKT)
        char* wkt = get_projection_wkt(meta);
        if (wkt) {
            GDALSetProjection(ds, wkt);
            CPLFree(wkt);
        }

        // 2. Configurar GeoTransform
        double gt[6];
        memcpy(gt, meta->geotransform, sizeof(double) * 6);

        // --- CONVERSIÓN DE UNIDADES (RAD -> METROS) ---
        // El archivo NetCDF tiene coordenadas en Radianes.
        // La proyección PROJ.4 (+proj=geos) espera Metros.
        if (meta->proj_code == PROJ_GEOS && meta->proj_info.valid) {
            double h = meta->proj_info.sat_height;
            // Escalamos todos los componentes del geotransform
            gt[0] *= h; // Origen X
            gt[1] *= h; // Pixel Width
            gt[2] *= h; // Rotación X
            gt[3] *= h; // Origen Y
            gt[4] *= h; // Rotación Y
            gt[5] *= h; // Pixel Height
        }

        // --- AJUSTE DE RECORTE (CROP) ---
        gt[0] = gt[0] + (offset_x * gt[1]);
        gt[3] = gt[3] + (offset_y * gt[5]);

        GDALSetGeoTransform(ds, gt);

        // 3. Metadatos internos (satélite, sector, banda)
        set_gdal_metadata(ds, meta);
    }

    return ds;
}

/**
 * Copia el dataset MEM a un archivo COG (Cloud Optimized GeoTIFF).
 * El driver COG genera tiling y overviews automáticamente.
 * Cierra el dataset MEM al finalizar.
 */
static int finalize_cog(GDALDatasetH mem_ds, const char* filename) {
    GDALDriverH cog_driver = GDALGetDriverByName("COG");
    if (!cog_driver) {
        LOG_ERROR("Driver COG no disponible en GDAL.");
        GDALClose(mem_ds);
        return -1;
    }

    char **opts = NULL;
    opts = CSLSetNameValue(opts, "COMPRESS", "ZSTD");
    opts = CSLSetNameValue(opts, "PREDICTOR", "2");
    opts = CSLSetNameValue(opts, "LEVEL", "6");
    opts = CSLSetNameValue(opts, "OVERVIEWS", "IGNORE_EXISTING");

    double t0 = omp_get_wtime();
    GDALDatasetH cog_ds = GDALCreateCopy(cog_driver, filename, mem_ds, FALSE, opts, NULL, NULL);
    LOG_TIMING(omp_get_wtime() - t0, "COG escrito: %s", filename);
    CSLDestroy(opts);
    GDALClose(mem_ds);

    if (!cog_ds) {
        LOG_ERROR("No se pudo crear el archivo COG: %s", filename);
        return -1;
    }

    int w = GDALGetRasterXSize(cog_ds);
    int h = GDALGetRasterYSize(cog_ds);
    int b = GDALGetRasterCount(cog_ds);
    GDALClose(cog_ds);
    LOG_INFO("GeoTIFF guardado: %s (%dx%d, %d banda%s)", filename, w, h, b, b == 1 ? "" : "s");
    return 0;
}

// --- Implementación de Funciones Públicas ---

int write_geotiff_rgb(const char* filename, const ImageData* img, const DataNC* meta,
                      int offset_x, int offset_y) {
    if (!img || (img->bpp != 3 && img->bpp != 4)) {
        LOG_ERROR("Imagen inválida para write_geotiff_rgb (se requiere bpp=3 o bpp=4)");
        return -1;
    }

    // Crear dataset en memoria con 3 o 4 bandas según si hay alpha
    int num_bands = img->bpp;
    GDALDatasetH ds = create_mem_dataset(img->width, img->height, num_bands, GDT_Byte, meta, offset_x, offset_y);
    if (!ds) return -1;

    // Escribir canales RGB (y alpha si existe)
    CPLErr err = CE_None;
    for (int i = 0; i < num_bands; i++) {
        GDALRasterBandH band = GDALGetRasterBand(ds, i + 1);
        err = GDALRasterIO(band, GF_Write, 0, 0, img->width, img->height, 
                           (void*)(img->data + i), 
                           img->width, img->height, GDT_Byte, 
                           num_bands, num_bands * img->width); // Interleaved
        if (err != CE_None) break;
        
        // Marcar el canal alpha si es el último y hay 4 bandas
        if (i == 3 && num_bands == 4) {
            GDALSetRasterColorInterpretation(band, GCI_AlphaBand);
        }
    }

    if (err != CE_None) {
        GDALClose(ds);
        return -1;
    }
    return finalize_cog(ds, filename);
}

int write_geotiff_gray(const char* filename, const ImageData* img, const DataNC* meta,
                       int offset_x, int offset_y) {
    if (!img || (img->bpp != 1 && img->bpp != 2)) {
        LOG_ERROR("Imagen inválida para write_geotiff_gray (se requiere bpp=1 o bpp=2)");
        return -1;
    }

    // Crear dataset en memoria con 1 o 2 bandas según si hay alpha
    int num_bands = img->bpp;
    GDALDatasetH ds = create_mem_dataset(img->width, img->height, num_bands, GDT_Byte, meta, offset_x, offset_y);
    if (!ds) return -1;

    CPLErr err = CE_None;
    
    if (img->bpp == 1) {
        // Caso simple: solo escala de grises
        GDALRasterBandH band = GDALGetRasterBand(ds, 1);
        err = GDALRasterIO(band, GF_Write, 0, 0, img->width, img->height, 
                          (void*)img->data, 
                          img->width, img->height, GDT_Byte, 
                          0, 0);
    } else {
        // Caso con alpha: escribir banda gray y banda alpha
        GDALRasterBandH gray_band = GDALGetRasterBand(ds, 1);
        GDALRasterBandH alpha_band = GDALGetRasterBand(ds, 2);
        
        // Escribir gray (pixel stride=2, line stride=2*width)
        err = GDALRasterIO(gray_band, GF_Write, 0, 0, img->width, img->height, 
                          (void*)img->data, 
                          img->width, img->height, GDT_Byte, 
                          2, 2 * img->width);
        
        if (err == CE_None) {
            // Escribir alpha (pixel stride=2, line stride=2*width, offset=1)
            err = GDALRasterIO(alpha_band, GF_Write, 0, 0, img->width, img->height, 
                              (void*)(img->data + 1), 
                              img->width, img->height, GDT_Byte, 
                              2, 2 * img->width);
            
            // Marcar la banda como alpha
            GDALSetRasterColorInterpretation(alpha_band, GCI_AlphaBand);
        }
    }

    if (err != CE_None) {
        GDALClose(ds);
        return -1;
    }
    return finalize_cog(ds, filename);
}

int write_geotiff_indexed(const char* filename, const ImageData* img, const ColorArray* palette,
                          const DataNC* meta, int offset_x, int offset_y) {
    if (!img || img->bpp != 1) {
        LOG_ERROR("Imagen inválida para write_geotiff_indexed (se requiere bpp=1)");
        return -1;
    }

    GDALDatasetH ds = create_mem_dataset(img->width, img->height, 1, GDT_Byte, meta, offset_x, offset_y);
    if (!ds) return -1;

    GDALRasterBandH band = GDALGetRasterBand(ds, 1);

    if (palette) {
        GDALColorTableH ct = GDALCreateColorTable(GPI_RGB);
        for (unsigned i = 0; i < palette->length; i++) {
            GDALColorEntry e = {palette->colors[i].r, palette->colors[i].g, palette->colors[i].b, 255};
            GDALSetColorEntry(ct, i, &e);
        }
        GDALSetRasterColorTable(band, ct);
        GDALDestroyColorTable(ct);
        GDALSetRasterColorInterpretation(band, GCI_PaletteIndex);
    }

    CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, img->width, img->height, 
                              (void*)img->data, 
                              img->width, img->height, GDT_Byte, 
                              0, 0);

    if (err != CE_None) {
        GDALClose(ds);
        return -1;
    }
    return finalize_cog(ds, filename);
}
