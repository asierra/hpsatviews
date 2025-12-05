# Plan de Implementación para Escritura de GeoTIFF

Este documento detalla los pasos necesarios para añadir la capacidad de generar imágenes en formato GeoTIFF, como alternativa al formato PNG actual. El objetivo es producir ficheros georreferenciados correctamente para las proyecciones geoestacionaria y geográfica.

---

## 📋 Contexto del Sistema Actual

### Tipos de Imágenes Generadas
1. **RGB** (`bpp=3`): Truecolor, Ash, Nocturnal pseudocolor, Day/Night mask
2. **Indexadas** (`bpp=1` + `ColorArray`): Pseudocolor con paletas CPT dinámicas
3. **Escala de grises** (`bpp=1`): Singlegray sin paleta

### Ubicaciones de Escritura PNG Actual
- `processing.c` líneas 303-305: Para singlegray y pseudocolor
- `rgb.c` líneas 554, 653: Para composiciones RGB

### Datos Disponibles en Punto de Escritura
- `ImageData` con la imagen procesada
- `DataF navla, navlo` con navegación lat/lon calculada
- `char *filename` del archivo NetCDF de referencia
- `ColorArray *palette` (opcional, solo para pseudocolor)

---

## 🎯 Paso 1: Integrar Dependencia de GDAL

### Modificar `Makefile`

**Ubicación**: Líneas 1-3

**Cambios**:
```makefile
CC=gcc
CFLAGS=-g -I. -Wall -std=c11 -fopenmp $(shell gdal-config --cflags)
LDFLAGS=-lm -lnetcdf -lpng -fopenmp $(shell gdal-config --libs)
```

**Ubicación**: Línea 9 (DEPS)
```makefile
DEPS = args.h datanc.h image.h logger.h processing.h reader_cpt.h \
       reader_nc.h reprojection.h rayleigh.h rgb.h writer_png.h \
       filename_utils.h rayleigh_lut_embedded.h projection_utils.h \
       writer_geotiff.h
```

**Ubicación**: Línea 31 (OBJS, antes de writer_png.o)
```makefile
       truecolor_rgb.o \
       projection_utils.o \
       writer_geotiff.o \
       writer_png.o \
```

---

## 🎯 Paso 2: Módulo de Utilidades de Proyección

### Crear `projection_utils.h`

**Ubicación**: Archivo nuevo en raíz del proyecto

**Contenido completo**:
```c
/*
 * Projection utilities for GeoTIFF georeferencing
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_PROJECTION_UTILS_H_
#define HPSATVIEWS_PROJECTION_UTILS_H_

#include "datanc.h"

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
 * @param geotransform Array de salida con 6 elementos
 * @return 0 en éxito, código de error en fallo
 */
int compute_geotransform_geostationary(const char* nc_filename,
                                       unsigned width, unsigned height,
                                       double geotransform[6]);

/**
 * @brief Detecta si la navegación representa una grilla geográfica regular.
 * 
 * Verifica si navla/navlo representan una proyección geográfica (lat/lon uniformes)
 * o una proyección geoestacionaria (coordenadas irregulares).
 * 
 * @param navla Malla de latitudes
 * @param navlo Malla de longitudes
 * @return true si es grilla geográfica regular, false si es geoestacionaria
 */
bool is_geographic_grid(const DataF* navla, const DataF* navlo);

#endif /* HPSATVIEWS_PROJECTION_UTILS_H_ */
```

### Crear `projection_utils.c`

**Ubicación**: Archivo nuevo en raíz del proyecto

**Contenido completo**:
```c
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
    if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid)))
        ERR(retval);
    
    float perspective_height, semi_major, semi_minor, lon_origin, sweep_angle;
    
    if ((retval = nc_get_att_float(ncid, varid, "perspective_point_height", &perspective_height)))
        ERR(retval);
    if ((retval = nc_get_att_float(ncid, varid, "semi_major_axis", &semi_major)))
        ERR(retval);
    if ((retval = nc_get_att_float(ncid, varid, "semi_minor_axis", &semi_minor)))
        ERR(retval);
    if ((retval = nc_get_att_float(ncid, varid, "longitude_of_projection_origin", &lon_origin)))
        ERR(retval);
    
    // sweep_angle_axis: "x" o "y" (GOES usa "x")
    char sweep_axis[16];
    size_t len;
    if ((retval = nc_inq_attlen(ncid, varid, "sweep_angle_axis", &len))) {
        strcpy(sweep_axis, "x"); // Default
    } else {
        nc_get_att_text(ncid, varid, "sweep_angle_axis", sweep_axis);
        sweep_axis[len] = '\0';
    }
    
    nc_close(ncid);
    
    // Construir WKT para proyección Geostationary
    // Formato PROJ: +proj=geos +lon_0=<lon> +h=<height> +x_0=0 +y_0=0 +ellps=GRS80 +units=m +sweep=x
    char* wkt = malloc(1024);
    if (!wkt) {
        LOG_ERROR("Error de asignación de memoria para WKT");
        return NULL;
    }
    
    // Usar formato WKT2 simplificado compatible con GDAL
    snprintf(wkt, 1024,
        "PROJCS[\"GOES-R Geostationary Projection\","
        "GEOGCS[\"GRS 1980\","
        "DATUM[\"unnamed\","
        "SPHEROID[\"GRS80\",%.1f,%.10f]],"
        "PRIMEM[\"Greenwich\",0],"
        "UNIT[\"degree\",0.0174532925199433]],"
        "PROJECTION[\"Geostationary_Satellite\"],"
        "PARAMETER[\"central_meridian\",%.6f],"
        "PARAMETER[\"satellite_height\",%.1f],"
        "PARAMETER[\"false_easting\",0],"
        "PARAMETER[\"false_northing\",0],"
        "UNIT[\"metre\",1]]",
        semi_major,
        (semi_major - semi_minor) / semi_major,  // inverse flattening
        lon_origin,
        perspective_height);
    
    LOG_DEBUG("WKT geoestacionario generado: %s", wkt);
    return wkt;
}

void compute_geotransform_geographic(const DataF* navla, const DataF* navlo,
                                     double geotransform[6]) {
    // GeoTransform[0] = Top-left X (longitud mínima)
    // GeoTransform[1] = Pixel width en grados de longitud
    // GeoTransform[2] = Rotación (0 para grillas alineadas)
    // GeoTransform[3] = Top-left Y (latitud máxima)
    // GeoTransform[4] = Rotación (0 para grillas alineadas)
    // GeoTransform[5] = Pixel height en grados de latitud (negativo)
    
    geotransform[0] = navlo->fmin;  // Origen X (lon_min)
    geotransform[3] = navla->fmax;  // Origen Y (lat_max)
    geotransform[1] = (navlo->fmax - navlo->fmin) / (navlo->width - 1);   // Ancho del píxel
    geotransform[5] = -(navla->fmax - navla->fmin) / (navla->height - 1); // Alto del píxel (negativo)
    geotransform[2] = 0.0;  // Sin rotación
    geotransform[4] = 0.0;  // Sin rotación
    
    LOG_DEBUG("GeoTransform geográfico: [%.6f, %.9f, 0, %.6f, 0, %.9f]",
              geotransform[0], geotransform[1], geotransform[3], geotransform[5]);
}

int compute_geotransform_geostationary(const char* nc_filename,
                                       unsigned width, unsigned height,
                                       double geotransform[6]) {
    int ncid, xid, yid, retval;
    
    if ((retval = nc_open(nc_filename, NC_NOWRITE, &ncid)))
        ERR_INT(retval);
    
    // Obtener dimensiones y variables x, y
    size_t x_len, y_len;
    if ((retval = nc_inq_dimid(ncid, "x", &xid)))
        ERR_INT(retval);
    if ((retval = nc_inq_dimid(ncid, "y", &yid)))
        ERR_INT(retval);
    if ((retval = nc_inq_dimlen(ncid, xid, &x_len)))
        ERR_INT(retval);
    if ((retval = nc_inq_dimlen(ncid, yid, &y_len)))
        ERR_INT(retval);
    
    // Leer arrays x[] e y[] (coordenadas en radianes)
    float* x_coords = malloc(x_len * sizeof(float));
    float* y_coords = malloc(y_len * sizeof(float));
    
    int x_varid, y_varid;
    if ((retval = nc_inq_varid(ncid, "x", &x_varid)))
        ERR_INT(retval);
    if ((retval = nc_inq_varid(ncid, "y", &y_varid)))
        ERR_INT(retval);
    
    if ((retval = nc_get_var_float(ncid, x_varid, x_coords)))
        ERR_INT(retval);
    if ((retval = nc_get_var_float(ncid, y_varid, y_coords)))
        ERR_INT(retval);
    
    nc_close(ncid);
    
    // Calcular límites (x e y están en radianes)
    double x_min = x_coords[0];
    double x_max = x_coords[x_len - 1];
    double y_min = y_coords[y_len - 1];  // y suele estar invertido
    double y_max = y_coords[0];
    
    free(x_coords);
    free(y_coords);
    
    // GeoTransform para coordenadas geoestacionarias
    geotransform[0] = x_min;
    geotransform[3] = y_max;
    geotransform[1] = (x_max - x_min) / (width - 1);
    geotransform[5] = -(y_max - y_min) / (height - 1);
    geotransform[2] = 0.0;
    geotransform[4] = 0.0;
    
    LOG_DEBUG("GeoTransform geoestacionario: [%.9f, %.12f, 0, %.9f, 0, %.12f]",
              geotransform[0], geotransform[1], geotransform[3], geotransform[5]);
    
    return 0;
}

bool is_geographic_grid(const DataF* navla, const DataF* navlo) {
    // Una grilla geográfica tiene latitudes/longitudes uniformemente espaciadas
    // Verificamos algunas muestras para detectar regularidad
    
    if (!navla || !navlo || navla->width < 2 || navla->height < 2)
        return false;
    
    // Calcular espaciado esperado
    float expected_lon_step = (navlo->fmax - navlo->fmin) / (navlo->width - 1);
    float expected_lat_step = (navla->fmax - navla->fmin) / (navla->height - 1);
    
    // Muestrear algunos puntos en la primera fila
    float tolerance = 0.01; // 1% de variación permitida
    for (unsigned x = 0; x < navlo->width && x < 10; x++) {
        unsigned idx = x; // Primera fila
        float expected_lon = navlo->fmin + x * expected_lon_step;
        float actual_lon = navlo->data_in[idx];
        
        if (actual_lon == NonData)
            return false; // Grilla geográfica no debería tener NonData
        
        float diff = fabs(actual_lon - expected_lon);
        if (diff > fabs(expected_lon_step * tolerance))
            return false; // No es uniforme
    }
    
    // Si pasó las verificaciones, es geográfica
    return true;
}
```

---

## 🎯 Paso 3: Módulo writer_geotiff

### Crear `writer_geotiff.h`

**Ubicación**: Archivo nuevo en raíz del proyecto

**Contenido completo**:
```c
/*
 * GeoTIFF writer module
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_WRITER_GEOTIFF_H_
#define HPSATVIEWS_WRITER_GEOTIFF_H_

#include "image.h"
#include "datanc.h"

/**
 * @brief Escribe una imagen RGB a formato GeoTIFF georreferenciado.
 * 
 * Detecta automáticamente el tipo de proyección (geográfica vs geoestacionaria)
 * y genera el GeoTIFF apropiado.
 * 
 * @param filename Nombre del archivo de salida (.tif o .tiff)
 * @param img Imagen RGB (bpp=3) a escribir
 * @param navla Malla de latitudes (navegación)
 * @param navlo Malla de longitudes (navegación)
 * @param nc_reference_file Archivo NetCDF de referencia para metadatos de proyección
 * @return 0 en éxito, código de error en fallo
 */
int write_geotiff_rgb(const char* filename,
                     const ImageData* img,
                     const DataF* navla,
                     const DataF* navlo,
                     const char* nc_reference_file);

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
 * @return 0 en éxito, código de error en fallo
 */
int write_geotiff_gray(const char* filename,
                      const ImageData* img,
                      const DataF* navla,
                      const DataF* navlo,
                      const char* nc_reference_file);

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
 * @return 0 en éxito, código de error en fallo
 */
int write_geotiff_indexed(const char* filename,
                         const ImageData* img,
                         const ColorArray* palette,
                         const DataF* navla,
                         const DataF* navlo,
                         const char* nc_reference_file);

#endif /* HPSATVIEWS_WRITER_GEOTIFF_H_ */
```

### Crear `writer_geotiff.c`

**Ubicación**: Archivo nuevo en raíz del proyecto

**Contenido**: Implementar las 3 funciones usando GDAL API:

**Estructura común de cada función**:
1. Detectar proyección con `is_geographic_grid()`
2. Calcular WKT y GeoTransform apropiados
3. Crear dataset GDAL con `GDALCreate()`
4. Configurar proyección con `GDALSetProjection()` y `GDALSetGeoTransform()`
5. Escribir datos con `GDALRasterIO()`
6. Para indexadas: crear y asignar `GDALColorTable`
7. Cerrar dataset con `GDALClose()`

**Template base**:
```c
#include "writer_geotiff.h"
#include "projection_utils.h"
#include "logger.h"
#include <gdal.h>
#include <cpl_conv.h>
#include <stdlib.h>
#include <string.h>

int write_geotiff_rgb(const char* filename,
                     const ImageData* img,
                     const DataF* navla,
                     const DataF* navlo,
                     const char* nc_reference_file) {
    
    if (!filename || !img || !img->data || img->bpp != 3) {
        LOG_ERROR("Parámetros inválidos para write_geotiff_rgb");
        return -1;
    }
    
    GDALAllRegister();
    
    // 1. Detectar tipo de proyección
    bool is_geographic = is_geographic_grid(navla, navlo);
    
    // 2. Preparar WKT y GeoTransform
    char* wkt = NULL;
    double geotransform[6];
    
    if (is_geographic) {
        wkt = strdup("EPSG:4326");
        compute_geotransform_geographic(navla, navlo, geotransform);
    } else {
        wkt = build_geostationary_wkt_from_nc(nc_reference_file);
        if (compute_geotransform_geostationary(nc_reference_file, img->width, img->height, geotransform) != 0) {
            free(wkt);
            return -1;
        }
    }
    
    if (!wkt) {
        LOG_ERROR("Error al generar WKT de proyección");
        return -1;
    }
    
    // 3. Crear dataset GeoTIFF
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    GDALDatasetH dataset = GDALCreate(driver, filename, img->width, img->height,
                                     3, GDT_Byte, NULL);
    
    if (!dataset) {
        LOG_ERROR("Error al crear dataset GeoTIFF: %s", filename);
        free(wkt);
        return -1;
    }
    
    // 4. Configurar georreferencia
    GDALSetProjection(dataset, wkt);
    GDALSetGeoTransform(dataset, geotransform);
    
    // 5. Escribir datos RGB (separar canales)
    for (int band = 1; band <= 3; band++) {
        GDALRasterBandH band_h = GDALGetRasterBand(dataset, band);
        
        // Extraer canal (R=0, G=1, B=2)
        unsigned char* channel_data = malloc(img->width * img->height);
        for (unsigned i = 0; i < img->width * img->height; i++) {
            channel_data[i] = img->data[i * 3 + (band - 1)];
        }
        
        GDALRasterIO(band_h, GF_Write, 0, 0, img->width, img->height,
                    channel_data, img->width, img->height, GDT_Byte, 0, 0);
        
        free(channel_data);
    }
    
    // 6. Cerrar y limpiar
    GDALClose(dataset);
    free(wkt);
    
    LOG_INFO("GeoTIFF RGB guardado: %s (%ux%u, proyección: %s)",
             filename, img->width, img->height,
             is_geographic ? "geográfica" : "geoestacionaria");
    
    return 0;
}

// write_geotiff_gray(): Similar pero con 1 banda
// write_geotiff_indexed(): Similar pero añadir GDALColorTable desde ColorArray
```

---

## 🎯 Paso 4: Integración en Flujo de Ejecución

### Estrategia de Detección de Formato

**Dos mecanismos**:
1. **Por extensión**: Si `-o salida.tif` → GeoTIFF automático
2. **Por flag**: Si `--geotiff` (o `-t`) → GeoTIFF con nombre autogenerado

Esto permite ambos casos de uso:
```bash
# Caso 1: Nombre explícito con extensión
./hpsatviews rgb -m ash -o resultado.tif archivo.nc

# Caso 2: Flag para nombre autogenerado
./hpsatviews rgb -m ash --geotiff archivo.nc
# Genera: out20253341234-ash.tif (en vez de .png)
```

### Modificar `main.c`

**Ubicación**: Después de línea 37 (dentro de función `main`, al crear comandos)

**Para cada comando** (`rgb_cmd`, `sg_cmd`, `pc_cmd`), añadir el flag `--geotiff`:

```c
// En la sección de comando rgb (alrededor línea 51)
ap_add_flag(rgb_cmd, "geotiff t");

// En la sección de comando pseudocolor (alrededor línea 97)
ap_add_flag(pc_cmd, "geotiff t");

// En la sección de comando singlegray (alrededor línea 113)
ap_add_flag(sg_cmd, "geotiff t");
```

**Descripción del flag para helptext**: "Generar salida en formato GeoTIFF (en vez de PNG)"

### Modificar `filename_utils.c`

**Ubicación**: Línea 58 (función `generate_default_output_filename`)

**Añadir parámetro** para especificar extensión:

**Firma actual**:
```c
char* generate_default_output_filename(const char* input_file_path, 
                                       const char* processing_mode, 
                                       const char* output_extension)
```

**No requiere cambios** - Ya acepta `output_extension` como parámetro. Perfecto!

### Modificar `processing.c`

**Ubicación**: Línea 16 (includes)
```c
#include "writer_png.h"
#include "writer_geotiff.h"
```

**Ubicación**: Líneas 59-85 (generación de nombre por defecto)

**Código antiguo**:
```c
    if (ap_found(parser, "out")) {
        outfn = ap_get_str_value(parser, "out");
    } else {
        // Generar nombre de archivo por defecto si no fue proporcionado
        const char* mode_name = is_pseudocolor ? "pseudocolor" : "singlegray";
        char* base_filename = generate_default_output_filename(fnc01, mode_name, ".png");
        
        // Si hay reproyección, agregar sufijo _geo antes de la extensión
        if (do_reprojection && base_filename) {
            // ... código para añadir _geo ...
        }
        
        outfn = out_filename_generated = base_filename;
    }
```

**Código nuevo**:
```c
    // Determinar formato de salida
    bool force_geotiff = ap_found(parser, "geotiff");
    
    if (ap_found(parser, "out")) {
        outfn = ap_get_str_value(parser, "out");
    } else {
        // Generar nombre de archivo por defecto
        const char* mode_name = is_pseudocolor ? "pseudocolor" : "singlegray";
        const char* extension = force_geotiff ? ".tif" : ".png";
        char* base_filename = generate_default_output_filename(fnc01, mode_name, extension);
        
        // Si hay reproyección, agregar sufijo _geo antes de la extensión
        if (do_reprojection && base_filename) {
            char* dot = strrchr(base_filename, '.');
            if (dot) {
                size_t prefix_len = dot - base_filename;
                size_t total_len = strlen(base_filename) + 5;
                out_filename_generated = (char*)malloc(total_len);
                if (out_filename_generated) {
                    strncpy(out_filename_generated, base_filename, prefix_len);
                    out_filename_generated[prefix_len] = '\0';
                    strcat(out_filename_generated, "_geo");
                    strcat(out_filename_generated, dot);
                    free(base_filename);
                } else {
                    out_filename_generated = base_filename;
                }
            } else {
                out_filename_generated = base_filename;
            }
        } else {
            out_filename_generated = base_filename;
        }
        
        outfn = out_filename_generated;
    }
```

**Ubicación**: Líneas 302-306 (reemplazar lógica de escritura)

**Código antiguo**:
```c
    if (is_pseudocolor && color_array) {
        write_image_png_palette(outfn, &imout, color_array);
    } else {
        write_image_png(outfn, &imout);
    }
```

**Código nuevo**:
```c
    // Detectar formato: por flag --geotiff o por extensión .tif/.tiff
    bool is_geotiff = force_geotiff || 
                     (strstr(outfn, ".tif") != NULL || strstr(outfn, ".tiff") != NULL);
    
    if (is_geotiff) {
        // Escribir GeoTIFF
        if (is_pseudocolor && color_array) {
            write_geotiff_indexed(outfn, &imout, color_array, &navla, &navlo, fnc01);
        } else if (imout.bpp == 3) {
            write_geotiff_rgb(outfn, &imout, &navla, &navlo, fnc01);
        } else {
            write_geotiff_gray(outfn, &imout, &navla, &navlo, fnc01);
        }
    } else {
        // Escribir PNG (comportamiento existente)
        if (is_pseudocolor && color_array) {
            write_image_png_palette(outfn, &imout, color_array);
        } else {
            write_image_png(outfn, &imout);
        }
    }
```

**IMPORTANTE**: La variable `force_geotiff` debe declararse antes del bloque de generación de nombre.

### Modificar `rgb.c`

**Ubicación**: Línea 18 (includes)
```c
#include "writer_png.h"
#include "writer_geotiff.h"
```

**Ubicación**: Líneas 499-527 (generación de nombre por defecto)

**Añadir detección de flag** antes de generar nombre:

```c
    // Determinar formato de salida
    bool force_geotiff = ap_found(parser, "geotiff");
    
    if (ap_found(parser, "out")) {
        out_filename = ap_get_str_value(parser, "out");
    } else {
        // Generar nombre con extensión apropiada
        const char* extension = force_geotiff ? ".tif" : ".png";
        const char* filename_for_timestamp = c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : input_file;
        char* base_filename = generate_default_output_filename(filename_for_timestamp, mode, extension);
        
        // ... resto del código de _geo ...
    }
```

**Ubicación**: Línea 554 (escritura diurna)

**Código antiguo**:
```c
        write_image_png(out_filename, &diurna);
```

**Código nuevo**:
```c
        bool is_geotiff = force_geotiff || 
                         (strstr(out_filename, ".tif") != NULL || strstr(out_filename, ".tiff") != NULL);
        if (is_geotiff) {
            write_geotiff_rgb(out_filename, &diurna, &navla, &navlo, c_info[ref_ch_idx]->filename);
        } else {
            write_image_png(out_filename, &diurna);
        }
```

**Ubicación**: Línea 653 (escritura final)

**Código antiguo**:
```c
    write_image_png(out_filename, &final_image);
```

**Código nuevo**:
```c
    bool is_geotiff = force_geotiff || 
                     (strstr(out_filename, ".tif") != NULL || strstr(out_filename, ".tiff") != NULL);
    if (is_geotiff) {
        // Determinar archivo de referencia para navegación
        const char* nc_ref = c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : NULL;
        if (nc_ref) {
            write_geotiff_rgb(out_filename, &final_image, &navla, &navlo, nc_ref);
        } else {
            LOG_ERROR("No se puede escribir GeoTIFF sin archivo de referencia NetCDF");
        }
    } else {
        write_image_png(out_filename, &final_image);
    }
```

**IMPORTANTE**: La variable `force_geotiff` debe estar en el scope (declarada al inicio de la función).

---

## 🎯 Paso 5: Actualizar Documentación

### Modificar `README.md`

**Añadir sección nueva antes de "Ejemplos"**:

```markdown
### Formatos de Salida Soportados

#### PNG (por defecto)
```bash
./hpsatviews rgb -m ash -o salida.png archivo.nc
./hpsatviews singlegray archivo.nc  # Genera out<timestamp>-singlegray.png
```

#### GeoTIFF (georreferenciado)

**Opción 1: Extensión explícita**
```bash
# Nombre con extensión .tif o .tiff
./hpsatviews rgb -m ash -o salida.tif archivo.nc
./hpsatviews singlegray -o recorte.tiff archivo.nc --clip -107 22 -93 14
```

**Opción 2: Flag --geotiff (nombre autogenerado)**
```bash
# Genera out<timestamp>-ash.tif en vez de .png
./hpsatviews rgb -m ash --geotiff archivo.nc
./hpsatviews singlegray --geotiff -t archivo.nc  # -t es alias de --geotiff

# Con reproyección: genera out<timestamp>-truecolor_geo.tif
./hpsatviews rgb -m truecolor -r --geotiff archivo.nc
```

**Combinaciones válidas**:
```bash
# Con recorte geográfico
./hpsatviews rgb -m ash --clip -107 22 -93 14 -t archivo.nc

# Con reproyección geográfica
./hpsatviews rgb -m truecolor -r --geotiff archivo.nc

# Pseudocolor con paleta CPT
./hpsatviews pseudocolor -p paleta.cpt --geotiff archivo.nc
```

**Proyecciones en GeoTIFF**:
- **Geográfica** (EPSG:4326): Cuando se usa flag `-r` o `--geographics`
- **Geoestacionaria** (GOES native): Proyección nativa del satélite sin reproyección
- Ambas incluyen metadatos completos de georreferenciación compatibles con QGIS, ArcGIS, GDAL, etc.
```

---

## ✅ Verificación de Implementación

### Tests Recomendados

1. **RGB geoestacionario con nombre explícito**:
```bash
./hpsatviews rgb -m ash -o test_geost.tif archivo_L2.nc
gdalinfo test_geost.tif  # Verificar proyección geoestacionaria
```

2. **RGB geográfico con flag --geotiff**:
```bash
./hpsatviews rgb -m truecolor -r --geotiff archivo_L1b.nc
# Genera: out<timestamp>-truecolor_geo.tif
gdalinfo out*.tif  # Verificar EPSG:4326
```

3. **Singlegray con nombre corto -t**:
```bash
./hpsatviews singlegray -t archivo.nc
# Genera: out<timestamp>-singlegray.tif
gdalinfo out*.tif
```

4. **Pseudocolor indexado**:
```bash
./hpsatviews pseudocolor -p paleta.cpt --geotiff archivo.nc
gdalinfo out*-pseudocolor.tif  # Verificar Color Table
```

5. **Comparación con GDAL**:
```bash
# Comparar con resultado de gdalwarp directo desde NetCDF
gdalwarp NETCDF:"archivo.nc":CMI reference.tif
# Visualizar ambos en QGIS y verificar alineación geográfica
```

---

## 📝 Notas de Implementación

### Orden de Desarrollo Recomendado

1. ✅ **Paso 1**: Makefile (5 min)
2. ✅ **Paso 2**: projection_utils.h/.c (30 min) - Empezar aquí, es la base
3. ✅ **Paso 3**: writer_geotiff.h (10 min), luego writer_geotiff.c (60 min)
4. ✅ **Paso 4**: Integración en processing.c y rgb.c (20 min)
5. ✅ **Paso 5**: README.md (10 min)

**Tiempo estimado total**: ~2 horas

### Puntos Críticos de Atención

1. **Gestión de memoria en `navla`/`navlo`**: Verificar que no se destruyan antes de escribir GeoTIFF
2. **Orden de bandas en RGB**: GDAL espera R, G, B en bandas separadas (no entrelazadas)
3. **GeoTransform de geoestacionaria**: Requiere leer arrays `x[]` e `y[]` del NetCDF
4. **Color Table**: `ColorArray` ya está en [0,255], copiar directo a `GDALColorEntry`
5. **WKT vs PROJ**: GDAL prefiere WKT2, pero acepta PROJ strings con `+proj=geos`

### Dependencias Externas

- **libgdal-dev** (Debian/Ubuntu): `sudo apt install libgdal-dev`
- Headers en: `/usr/include/gdal/`
- Verificar versión: `gdal-config --version` (recomendado >= 3.0)

---

## 🔍 Resolución de Problemas Comunes

### Error: "GTiff driver not found"
```bash
# Verificar drivers disponibles
gdalinfo --formats | grep GTiff
# Si no aparece, reinstalar GDAL
```

### Error: Proyección no reconocida en QGIS
- Verificar WKT con: `gdalsrsinfo salida.tif`
- Comparar con WKT de GDAL: `gdalsrsinfo -o wkt EPSG:4326`

### Imagen georreferenciada en ubicación incorrecta
- Revisar GeoTransform: `gdalinfo salida.tif | grep -A6 "Origin"`
- Verificar que `navla->fmin/fmax` sean correctos
- Para geoestacionaria: verificar que x/y estén en radianes

### Color Table no se muestra
- Verificar: `gdalinfo -hist salida.tif | grep "Color Table"`
- Asegurarse que `GDALSetRasterColorTable()` se llame ANTES de `GDALClose()`
