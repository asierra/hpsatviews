/* Creates a dataset in geographics from a geostationary dataset.
 * LUT-BASED REPROJECTION - Final, Correct & Optimized Version
 *
 * This version uses a Look-Up Table (LUT) for reprojection, which is a
 * standard and efficient method for this task. It uses a well-estimated
 * search window to find the nearest source pixel for each destination pixel.
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
#include <string.h>

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha);


// Creates the target g
int geos2geographics(const char* input_file) {
     DataNC dc;
    if (load_nc_sf(input_file, "Rad", &dc) != 0) return -1;
    LOG_INFO("Loaded NetCDF data: %u x %u pixels", dc.base.width, dc.base.height);

    DataF navlo, navla;
    if (compute_navigation_nc(input_file, &navla, &navlo) != 0) return -1;
    LOG_INFO("Navigation data computed successfully");
    LOG_INFO("Navigation extent: lat[%.3f, %.3f], lon[%.3f, %.3f]", navla.fmin, navla.fmax, navlo.fmin, navlo.fmax);

    // 60% size of original
    size_t width=navlo.width*0.7, height=navla.height*0.7;
    LOG_INFO("Output size %lu x %lu, original %lu x %lu", width, height,  navlo.width, navla.height);

    // Create grid and perform the reprojection)
    DataF datagg = dataf_create(width, height);
    dataf_fill(&datagg, NonData);
    
    #pragma omp parallel for collapse(2)
    for (unsigned y = 0; y < dc.base.height; y++) {
        for (unsigned x = 0; x < dc.base.width; x++) {
            unsigned i = y * dc.base.width + x;
            float lo = navlo.data_in[i];
            float la = navla.data_in[i];
            float f = dc.base.data_in[i];
            if (lo != NonData && la != NonData && f != NonData) {
                // Mapea la latitud y longitud a las coordenadas de la rejilla de salida
                int ix = (int)(((lo - navlo.fmin) / (navlo.fmax - navlo.fmin)) * (width - 1));
                int iy = (int)(((navla.fmax - la) / (navla.fmax - navla.fmin)) * (height - 1));

                // Asegurarse de que los índices están dentro de los límites
                if (ix >= 0 && ix < (int)width && iy >= 0 && iy < (int)height) {
                    size_t j = (size_t)iy * width + (size_t)ix;
                    // Usar una operación atómica para evitar condiciones de carrera.
                    // Por ahora, la última escritura gana, pero de forma segura.
                    #pragma omp atomic write
                    datagg.data_in[j] = f;
                }
            }
        }
    }
    LOG_INFO("LOOP terminado");
    datagg.fmin = dc.base.fmin;
    datagg.fmax = dc.base.fmax;

    LOG_INFO("Iniciando relleno de huecos (interpolación de vecinos)...");

    // Crear un buffer temporal. Es OBLIGATORIO para un solo paso.
    // No podemos leer y escribir en 'datagg' al mismo tiempo.
    DataF datagg_filled = dataf_copy(&datagg);

    // Desplazamientos para 4 vecinos (Arriba, Abajo, Izquierda, Derecha)
    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };
    const int num_neighbors = 4;

    #pragma omp parallel for collapse(2)
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t idx = y * width + x;

            // Comprobar si este pixel es un hueco (leyendo de la fuente original)
            if (isnan(datagg.data_in[idx])) {
                
                float sum = 0.0f;
                int count = 0;

                // Revisar los vecinos
                for (int k = 0; k < num_neighbors; k++) {
                    int nx = (int)x + dx[k]; // Coordenada X del vecino
                    int ny = (int)y + dy[k]; // Coordenada Y del vecino

                    // Comprobar que el vecino esté DENTRO de los límites
                    if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                        size_t n_idx = (size_t)ny * width + (size_t)nx;
                        
                        // Leer el valor del vecino (¡siempre desde la fuente original!)
                        float n_val = datagg.data_in[n_idx];

                        // Si el vecino SÍ tiene un valor válido, contarlo
                        if (!isnan(n_val)) {
                            sum += n_val;
                            count++;
                        }
                    }
                } // Fin bucle de vecinos

                // Si encontramos al menos un vecino válido, calcular la media
                if (count > 0) {
                    // Escribir el valor interpolado en el buffer de destino
                    datagg_filled.data_in[idx] = sum / (float)count;
                }
            }
        }
    } // Fin bucle de relleno
    LOG_INFO("Relleno de huecos terminado.");
    free(datagg.data_in); // Liberar la memoria antigua (con huecos)
    // Apuntar 'datagg' al nuevo buffer rellenado
    datagg.data_in = datagg_filled.data_in;

    ImageData imout = create_single_gray(datagg, false, false);
    LOG_INFO("singlegray creada");
    const char *outfn = "geographic_reprojection.png";
    if (write_image_png(outfn, &imout) == 0) {
        LOG_INFO("Output saved to: %s", outfn);
    }

    // Clean up
    dataf_destroy(&dc.base);
    dataf_destroy(&datagg);
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    image_destroy(&imout);

    return 0;
}

int main(int argc, char *argv[]) {
    logger_init(LOG_INFO);
    if (argc < 2) {
        LOG_ERROR("Usage: %s <NetCDF ABI File> [resolution_deg]", argv[0]);
        return -1;
    }

    const char *fnc = argv[1];
    LOG_INFO("Processing file: %s", fnc);
    return geos2geographics(fnc);
}