/* Creates a dataset in geographics from a geostationary dataset.
 * CORRECTED VERSION - Proper geostationary to geographic reprojection
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "reader_nc.h"
#include "writer_png.h"
#include "logger.h"
#include "image.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

float dataf_value(DataF data, unsigned i, unsigned j) {
  if (i >= data.width || j >= data.height) return NonData;
  unsigned ii = j * data.width + i;
  return data.data_in[ii];
}

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha);

// Estructura para definir los parámetros del grid geográfico
typedef struct {
    float lat_min, lat_max;
    float lon_min, lon_max;
    unsigned lat_points, lon_points;
    float lat_resolution, lon_resolution;
} GeographicGrid;

// Función para crear un grid geográfico basado en los datos de navegación
GeographicGrid create_geographic_grid(DataF navla, DataF navlo, float resolution_deg) {
    GeographicGrid grid;
    
    // Encontrar extremos reales de los datos
    grid.lat_min = navla.fmin;
    grid.lat_max = navla.fmax;
    grid.lon_min = navlo.fmin; 
    grid.lon_max = navlo.fmax;
    
    // Calcular resolución y dimensiones
    grid.lat_resolution = resolution_deg;
    grid.lon_resolution = resolution_deg;
    
    grid.lat_points = (unsigned)ceil((grid.lat_max - grid.lat_min) / grid.lat_resolution) + 1;
    grid.lon_points = (unsigned)ceil((grid.lon_max - grid.lon_min) / grid.lon_resolution) + 1;
    
    LOG_INFO("Geographic grid: lat[%.3f, %.3f] lon[%.3f, %.3f]", 
             grid.lat_min, grid.lat_max, grid.lon_min, grid.lon_max);
    LOG_INFO("Grid dimensions: %u x %u points, resolution: %.3f°", 
             grid.lon_points, grid.lat_points, resolution_deg);
    
    return grid;
}

// Interpolación bilinear ponderada por distancia para obtener un valor suavizado
float bilinear_interpolation(float target_lat, float target_lon,
                            DataF datag, DataF navla, DataF navlo,
                            float max_search_radius) {
    float sum_values = 0.0f;
    float sum_weights = 0.0f;
    int valid_points = 0;
    
    for (int j = 0; j < navla.height; j++) {
        for (int i = 0; i < navla.width; i++) {
            unsigned idx = j * navla.width + i;
            float pixel_lat = navla.data_in[idx];
            float pixel_lon = navlo.data_in[idx];
            float pixel_value = datag.data_in[idx];
            
            // Verificar validez de datos
            if (pixel_lat == NonData || pixel_lon == NonData || pixel_value == NonData) continue;
            
            // Calcular distancia euclidiana
            float dlat = target_lat - pixel_lat;
            float dlon = target_lon - pixel_lon;
            float distance = sqrt(dlat*dlat + dlon*dlon);
            
            // Solo usar puntos dentro del radio de búsqueda
            if (distance > max_search_radius) continue;
            
            // Peso inversamente proporcional a la distancia
            float weight = (distance < 1e-6f) ? 1e6f : 1.0f / distance;
            sum_values += pixel_value * weight;
            sum_weights += weight;
            valid_points++;
        }
    }
    
    return (valid_points > 0 && sum_weights > 0) ? sum_values / sum_weights : NonData;
}

DataF geos2geographics(DataF datag, DataF navla, DataF navlo, float resolution_deg) {
    LOG_INFO("Starting geostationary to geographic reprojection");
    
    // Crear grid geográfico
    GeographicGrid grid = create_geographic_grid(navla, navlo, resolution_deg);
    
    // Crear estructura de salida usando constructor seguro
    DataF datanc = dataf_create(grid.lon_points, grid.lat_points);
    datanc.fmin = datag.fmin;
    datanc.fmax = datag.fmax;
    
    unsigned valid_pixels = 0;
    unsigned missing_pixels = 0;
    double start = omp_get_wtime();
    
    // Radio máximo de búsqueda para interpolación (en grados)
    float max_search_radius = resolution_deg * 2.0f;
    
    LOG_INFO("Processing %u x %u geographic grid points", grid.lon_points, grid.lat_points);
    
    // Paralelizar por filas de latitud
    #pragma omp parallel for shared(datanc, datag, navla, navlo, grid) \
            reduction(+:valid_pixels,missing_pixels)
    for (int lat_idx = 0; lat_idx < (int)grid.lat_points; lat_idx++) {
        float target_lat = grid.lat_min + lat_idx * grid.lat_resolution;
        
        for (int lon_idx = 0; lon_idx < (int)grid.lon_points; lon_idx++) {
            float target_lon = grid.lon_min + lon_idx * grid.lon_resolution;
            
            // Índice en el array de salida
            unsigned out_idx = lat_idx * grid.lon_points + lon_idx;
            
            // Usar interpolación bilineal para obtener el valor
            float interpolated_value = bilinear_interpolation(target_lat, target_lon,
                                                            datag, navla, navlo,
                                                            max_search_radius);
            
            datanc.data_in[out_idx] = interpolated_value;
            
            if (interpolated_value != NonData) {
                valid_pixels++;
            } else {
                missing_pixels++;
            }
        }
        
        // Progreso cada 10% de las filas
        if (lat_idx % (grid.lat_points / 10 + 1) == 0) {
            LOG_DEBUG("Processed row %d/%u (%.1f%%)", lat_idx, grid.lat_points, 
                     100.0f * lat_idx / grid.lat_points);
        }
    }
    
    double end = omp_get_wtime();
    
    LOG_INFO("Reprojection completed in %.2f seconds", end - start);
    LOG_INFO("Valid pixels: %u, Missing pixels: %u (%.1f%% coverage)", 
             valid_pixels, missing_pixels, 
             100.0f * valid_pixels / (valid_pixels + missing_pixels));
    
    return datanc;
}

int main(int argc, char *argv[]) {
    // Inicializar logger
    logger_init(LOG_INFO);
    
    if (argc < 2) {
        LOG_ERROR("Usage: %s <NetCDF ABI File> [resolution_deg]", argv[0]);
        return -1;
    }
    
    const char *fnc = argv[1];
    float resolution_deg = (argc > 2) ? atof(argv[2]) : 0.01f; // Default 0.01° (~1km)
    
    LOG_INFO("Processing file: %s", fnc);
    LOG_INFO("Target resolution: %.3f degrees", resolution_deg);
    
    // Cargar datos
    DataNC dc;
    if (load_nc_sf(fnc, "Rad", &dc) != 0) {
        LOG_ERROR("Failed to load NetCDF data");
        return -1;
    }
    LOG_INFO("Loaded NetCDF data: %u x %u pixels", dc.base.width, dc.base.height);
    
    // Obtener coordenadas de navegación
    DataF navlo, navla;
    if (compute_navigation_nc(fnc, &navla, &navlo) != 0) {
        LOG_ERROR("Failed to compute navigation data");
        return -1;
    }
    LOG_INFO("Navigation data computed successfully");
    
    // Realizar reproyección
    DataF datagg = geos2geographics(dc.base, navla, navlo, resolution_deg);
    
    // Crear imagen usando constructor seguro
    bool invert_values = true;
    bool use_alpha = true;
    ImageData imout = create_single_gray(datagg, invert_values, use_alpha);
    
    // Guardar resultado
    const char *outfn = "geographic_reprojection.png";
    if (write_image_png(outfn, &imout) == 0) {
        LOG_INFO("Output saved to: %s", outfn);
    } else {
        LOG_ERROR("Failed to save output image");
    }
    
    // Limpiar memoria usando destructores seguros
    dataf_destroy(&datagg);
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    image_destroy(&imout);
    
    return 0;
}
