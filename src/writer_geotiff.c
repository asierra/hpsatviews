/* GeoTIFF output writer via GDAL (RGB, grayscale, and indexed modes).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "writer_geotiff.h"
#include "logger.h"
#include "timing.h"
#include <gdal.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// --- Funciones Auxiliares Privadas ---

static void set_colormap_metadata(GDALDatasetH ds, const ColormapMeta *cm) {
    if (!ds || !cm) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", cm->val_min);
    GDALSetMetadataItem(ds, "colormap_min", buf, "");
    snprintf(buf, sizeof(buf), "%.6g", cm->val_max);
    GDALSetMetadataItem(ds, "colormap_max", buf, "");
    snprintf(buf, sizeof(buf), "%d", cm->num_colors);
    GDALSetMetadataItem(ds, "colormap_size", buf, "");
    if (cm->units && cm->units[0])
        GDALSetMetadataItem(ds, "colormap_units", cm->units, "");
}

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
            LOG_ERROR("Error importing PROJ.4 projection: %s", proj4);
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
/**
 * Abre un dataset MEM que APUNTA al buffer entrelazado de ImageData en vez de
 * copiarlo. El driver MEM acepta un puntero de host más los strides, así que
 * GDAL lee los píxeles donde ya están.
 *
 * Esto evita dos costos que antes eran invisibles porque el único timer del
 * writer arrancaba en GDALCreateCopy: la reserva de un segundo buffer del
 * tamaño de la imagen, y el de-interleave (una pasada por banda leyendo 1 de
 * cada `bands` bytes; 0.082 s por 140 MB medido, ~0.11 s en un full-disk).
 *
 * El dataset NO es dueño de la memoria: `data` debe seguir viva hasta el
 * GDALClose, cosa que se cumple porque el ImageData del llamador sobrevive a
 * toda la escritura. Devuelve NULL si falla, y el llamador cae al camino que
 * reserva y copia.
 */
static GDALDatasetH open_mem_wrapping(GDALDriverH driver, const unsigned char* data,
                                      int width, int height, int bands) {
    // Se usa GDALAddBand con DATAPOINTER en vez de la sintaxis de nombre de
    // archivo "MEM:::DATAPOINTER=...": GDAL bloquea esa última por defecto desde
    // hace varias versiones (haría que un nombre de archivo no confiable pudiera
    // apuntar a memoria arbitraria) y exigiría activar GDAL_MEM_ENABLE_OPEN.
    // AddBand no tiene ese problema porque el puntero no viene de una ruta.
    GDALDatasetH ds = GDALCreate(driver, "", width, height, 0 /* sin bandas */,
                                 GDT_Byte, NULL);
    if (!ds) return NULL;

    char pixoff[32], lineoff[32];
    snprintf(pixoff, sizeof(pixoff), "%d", bands);            // RGBRGB...: 1 píxel = bands bytes
    snprintf(lineoff, sizeof(lineoff), "%d", bands * width);  // 1 línea = bands*width bytes

    for (int i = 0; i < bands; i++) {
        // La banda i arranca en el byte i y avanza de `bands` en `bands`.
        char ptr[64] = {0};
        int n = CPLPrintPointer(ptr, (void*)(data + i), (int)sizeof(ptr) - 1);
        if (n <= 0 || n >= (int)sizeof(ptr)) { GDALClose(ds); return NULL; }
        ptr[n] = '\0';

        char** opts = NULL;
        opts = CSLSetNameValue(opts, "DATAPOINTER", ptr);
        opts = CSLSetNameValue(opts, "PIXELOFFSET", pixoff);
        opts = CSLSetNameValue(opts, "LINEOFFSET", lineoff);
        CPLErr e = GDALAddBand(ds, GDT_Byte, opts);
        CSLDestroy(opts);
        if (e != CE_None) { GDALClose(ds); return NULL; }
    }
    return ds;
}

/**
 * @param data         Si no es NULL (y type es GDT_Byte), intenta envolver ese
 *                     buffer entrelazado sin copiarlo.
 * @param out_wrapped  Devuelve true si lo logró; en ese caso el dataset ya trae
 *                     los píxeles y el llamador NO debe escribir las bandas. Si
 *                     es false (o data era NULL) el dataset viene vacío y hay que
 *                     llenarlo con GDALRasterIO como siempre.
 */
static GDALDatasetH create_mem_dataset(int width,
                                       int height,
                                       int bands,
                                       GDALDataType type,
                                       const DataNC* meta,
                                       int offset_x,
                                       int offset_y,
                                       const unsigned char* data,
                                       bool* out_wrapped) {

    if (out_wrapped) *out_wrapped = false;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("MEM");
    if (!driver) {
        LOG_ERROR("MEM driver not available in GDAL.");
        return NULL;
    }

    GDALDatasetH ds = NULL;
    // HPSV_NO_MEM_ZEROCOPY=1 fuerza el camino que copia (para A/B de rendimiento).
    if (data && type == GDT_Byte && !getenv("HPSV_NO_MEM_ZEROCOPY")) {
        ds = open_mem_wrapping(driver, data, width, height, bands);
        if (ds) {
            if (out_wrapped) *out_wrapped = true;
        } else {
            // No es fatal: se cae al camino de reservar y copiar.
            LOG_WARN("MEM zero-copy no disponible; se copiará la imagen banda por banda.");
        }
    }
    if (!ds) ds = GDALCreate(driver, "", width, height, bands, type, NULL);
    if (!ds) {
        LOG_ERROR("Could not create in-memory dataset.");
        return NULL;
    }

    if (meta) {
        // 1. Set projection (WKT).
        char* wkt = get_projection_wkt(meta);
        if (wkt) {
            GDALSetProjection(ds, wkt);
            CPLFree(wkt);
        }

        // 2. Configurar GeoTransform
        double gt[6];
        memcpy(gt, meta->geotransform, sizeof(double) * 6);

        // --- UNIT CONVERSION (radians -> metres) ---
        // The NetCDF geotransform is in radians; PROJ (+proj=geos) requires metres.
        if (meta->proj_code == PROJ_GEOS && meta->proj_info.valid) {
            double h = meta->proj_info.sat_height;
            gt[0] *= h; // origin X
            gt[1] *= h; // pixel width
            gt[2] *= h; // rotation X
            gt[3] *= h; // origin Y
            gt[4] *= h; // rotation Y
            gt[5] *= h; // pixel height
        }

        // --- AJUSTE DE RECORTE (CROP) ---
        gt[0] = gt[0] + (offset_x * gt[1]);
        gt[3] = gt[3] + (offset_y * gt[5]);

        GDALSetGeoTransform(ds, gt);

        // 3. Internal metadata (satellite, sector, band).
        set_gdal_metadata(ds, meta);
    }

    return ds;
}

/**
 * Copia el dataset MEM a un GeoTIFF tileado (driver COG), comprimido ZSTD y
 * multi-hilo. Cierra el dataset MEM al finalizar.
 *
 * @param cog  Si es true genera los overviews (Cloud Optimized GeoTIFF completo,
 *             para cuando el GeoTIFF es el producto final). Si es false los omite:
 *             la pirámide de overviews es ~90% del costo de escritura en una
 *             escena de disco completo y es trabajo desperdiciado cuando el
 *             GeoTIFF es intermedio y se recorta aguas abajo.
 */
static int finalize_cog(GDALDatasetH mem_ds, const char* filename, bool cog) {
    GDALDriverH cog_driver = GDALGetDriverByName("COG");
    if (!cog_driver) {
        LOG_ERROR("COG driver not available in GDAL.");
        GDALClose(mem_ds);
        return -1;
    }

    char **opts = NULL;
    opts = CSLSetNameValue(opts, "COMPRESS", "ZSTD");
    opts = CSLSetNameValue(opts, "PREDICTOR", "2");
    opts = CSLSetNameValue(opts, "LEVEL", "6");
    opts = CSLSetNameValue(opts, "OVERVIEWS", cog ? "IGNORE_EXISTING" : "NONE");
    opts = CSLSetNameValue(opts, "NUM_THREADS", "ALL_CPUS");

    double t0 = omp_get_wtime();
    GDALDatasetH cog_ds = GDALCreateCopy(cog_driver, filename, mem_ds, FALSE, opts, NULL, NULL);
    LOG_TIMING_STAGE(TM_WRITE, omp_get_wtime() - t0, "%s written: %s", cog ? "COG (overviews)" : "GeoTIFF", filename);
    CSLDestroy(opts);
    GDALClose(mem_ds);

    if (!cog_ds) {
        LOG_ERROR("Could not create GeoTIFF file: %s", filename);
        return -1;
    }

    int w = GDALGetRasterXSize(cog_ds);
    int h = GDALGetRasterYSize(cog_ds);
    int b = GDALGetRasterCount(cog_ds);
    GDALClose(cog_ds);
    LOG_INFO("GeoTIFF saved: %s (%dx%d, %d band%s)", filename, w, h, b, b == 1 ? "" : "s");
    return 0;
}

// --- Public Function Implementations ---

int write_geotiff_rgb(const char* filename, const ImageData* img, const DataNC* meta,
                      int offset_x, int offset_y, const char* product, bool cog) {
    if (!img || (img->bpp != 3 && img->bpp != 4)) {
        LOG_ERROR("Invalid image for write_geotiff_rgb (bpp=3 or bpp=4 required).");
        return -1;
    }

    // Create in-memory dataset: 3 or 4 bands depending on alpha presence.
    int num_bands = img->bpp;
    double t_mem0 = omp_get_wtime();
    bool wrapped = false;
    GDALDatasetH ds = create_mem_dataset(img->width, img->height, num_bands, GDT_Byte,
                                         meta, offset_x, offset_y, img->data, &wrapped);
    if (!ds) return -1;

    if (product && product[0])
        GDALSetMetadataItem(ds, "product", product, "");

    // Con zero-copy el dataset ya ve los píxeles; solo queda marcar el alfa.
    // Sin él hay que de-interleavar: una pasada por banda leyendo 1 de cada
    // num_bands bytes. Se mide aparte porque el timer de finalize_cog() cubre
    // solo GDALCreateCopy y dejaba este costo sin atribuir.
    CPLErr err = CE_None;
    if (wrapped) {
        if (num_bands == 4)
            GDALSetRasterColorInterpretation(GDALGetRasterBand(ds, 4), GCI_AlphaBand);
    } else {
        for (int i = 0; i < num_bands; i++) {
            GDALRasterBandH band = GDALGetRasterBand(ds, i + 1);
            err = GDALRasterIO(band, GF_Write, 0, 0, img->width, img->height,
                               (void*)(img->data + i),
                               img->width, img->height, GDT_Byte,
                               num_bands, num_bands * img->width); // Interleaved
            if (err != CE_None) break;

            // Mark the alpha channel if it is the last of 4 bands.
            if (i == 3 && num_bands == 4) {
                GDALSetRasterColorInterpretation(band, GCI_AlphaBand);
            }
        }
    }
    LOG_TIMING_STAGE(TM_WRITE, omp_get_wtime() - t_mem0, "GeoTIFF MEM dataset (%d bandas, %ux%u, %s)",
               num_bands, img->width, img->height,
               wrapped ? "zero-copy" : "de-interleave");

    if (err != CE_None) {
        GDALClose(ds);
        return -1;
    }
    return finalize_cog(ds, filename, cog);
}

int write_geotiff_gray(const char* filename, const ImageData* img, const DataNC* meta,
                       int offset_x, int offset_y, const char* product, bool cog) {
    if (!img || (img->bpp != 1 && img->bpp != 2)) {
        LOG_ERROR("Invalid image for write_geotiff_gray (bpp=1 or bpp=2 required).");
        return -1;
    }

    // Create in-memory dataset: 1 or 2 bands depending on alpha presence.
    int num_bands = img->bpp;
    double t_mem0 = omp_get_wtime();
    bool wrapped = false;
    GDALDatasetH ds = create_mem_dataset(img->width, img->height, num_bands, GDT_Byte,
                                         meta, offset_x, offset_y, img->data, &wrapped);
    if (!ds) return -1;

    if (product && product[0])
        GDALSetMetadataItem(ds, "product", product, "");

    CPLErr err = CE_None;

    if (wrapped) {
        // El dataset ya ve los píxeles; solo falta declarar el alfa.
        if (num_bands == 2)
            GDALSetRasterColorInterpretation(GDALGetRasterBand(ds, 2), GCI_AlphaBand);
    } else if (img->bpp == 1) {
        // Grayscale only.
        GDALRasterBandH band = GDALGetRasterBand(ds, 1);
        err = GDALRasterIO(band, GF_Write, 0, 0, img->width, img->height, 
                          (void*)img->data, 
                          img->width, img->height, GDT_Byte, 
                          0, 0);
    } else {
        // Grayscale + alpha: write gray band and alpha band.
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
    LOG_TIMING_STAGE(TM_WRITE, omp_get_wtime() - t_mem0, "GeoTIFF MEM dataset (%d banda%s, %ux%u, %s)",
               num_bands, num_bands == 1 ? "" : "s", img->width, img->height,
               wrapped ? "zero-copy" : "copia");

    if (err != CE_None) {
        GDALClose(ds);
        return -1;
    }
    return finalize_cog(ds, filename, cog);
}

int write_geotiff_indexed(const char* filename, const ImageData* img, const ColorArray* palette,
                          const DataNC* meta, int offset_x, int offset_y,
                          const ColormapMeta* cm, const char* product, bool cog) {
    if (!img || img->bpp != 1) {
        LOG_ERROR("Invalid image for write_geotiff_indexed (bpp=1 required).");
        return -1;
    }

    // Sin zero-copy a propósito: es una sola banda (la copia es contigua, no hay
    // de-interleave que ahorrar) y este camino además cuelga una tabla de color
    // de la banda, así que no vale la pena tocarlo.
    GDALDatasetH ds = create_mem_dataset(img->width, img->height, 1, GDT_Byte, meta,
                                         offset_x, offset_y, NULL, NULL);
    if (!ds) return -1;

    if (product && product[0])
        GDALSetMetadataItem(ds, "product", product, "");

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

    if (cm && cm->has_nodata) {
        GDALSetRasterNoDataValue(band, (double)cm->nodata_index);
    }

    CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, img->width, img->height,
                              (void*)img->data,
                              img->width, img->height, GDT_Byte,
                              0, 0);

    if (err != CE_None) {
        GDALClose(ds);
        return -1;
    }
    set_colormap_metadata(ds, cm);
    return finalize_cog(ds, filename, cog);
}
