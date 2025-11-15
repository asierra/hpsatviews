/*
 * Geostationary to Geographics Reprojection Module
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "reprojection.h"
#include "datanc.h"
#include "reader_nc.h"
#include "logger.h"
#include <omp.h>
#include <math.h>
#include <stdlib.h>

/**
 * @brief Reprojects a DataF grid from geostationary to geographic coordinates.
 *
 * @param source_data The source data grid to reproject.
 * @param nav_reference_file A NetCDF file path used to compute the navigation grid (lat/lon).
 * @return A new DataF structure containing the reprojected and gap-filled data.
 *         The caller is responsible for freeing this structure with dataf_destroy().
 *         Returns an empty DataF on failure.
 */
DataF reproject_to_geographics(const DataF* source_data, const char* nav_reference_file) {
    LOG_INFO("Iniciando reproyección a geográficas...");

    DataF navla, navlo;
    if (compute_navigation_nc(nav_reference_file, &navla, &navlo) != 0) {
        LOG_ERROR("Fallo al calcular la navegación para la reproyección.");
        return dataf_create(0, 0, DATA_TYPE_FLOAT); // Retorna estructura vacía
    }
    LOG_INFO("Datos de navegación calculados. Extensión: lat[%.3f, %.3f], lon[%.3f, %.3f]", navla.fmin, navla.fmax, navlo.fmin, navlo.fmax);

    // Usar 70% del tamaño original para la salida
    size_t width = navlo.width * 0.7;
    size_t height = navla.height * 0.7;
    LOG_INFO("Tamaño de salida: %zu x %zu", width, height);

    DataF datagg = dataf_create(width, height, source_data->type);
    dataf_fill(&datagg, NonData);

    // Cast void pointers to float pointers before the loop for safety and clarity
    float* navlo_data = (float*)navlo.data_in;
    float* navla_data = (float*)navla.data_in;
    float* source_data_ptr = (float*)source_data->data_in;
    float* datagg_data = (float*)datagg.data_in;

    #pragma omp parallel for collapse(2)
    for (unsigned y = 0; y < source_data->height; y++) {
        for (unsigned x = 0; x < source_data->width; x++) {
            unsigned i = y * source_data->width + x;
            float lo = navlo_data[i];
            float la = navla_data[i];
            float f = source_data_ptr[i];
            if (lo != NonData && la != NonData && f != NonData) {
                int ix = (int)(((lo - navlo.fmin) / (navlo.fmax - navlo.fmin)) * (width - 1));
                int iy = (int)(((navla.fmax - la) / (navla.fmax - navla.fmin)) * (height - 1));

                if (ix >= 0 && ix < (int)width && iy >= 0 && iy < (int)height) {
                    size_t j = (size_t)iy * width + (size_t)ix;
                    // OMP atomic needs a direct memory reference of a known type
                    #pragma omp atomic write
                    datagg_data[j] = f;
                }
            }
        }
    }
    LOG_DEBUG("Bucle de reproyección terminado.");
    datagg.fmin = source_data->fmin;
    datagg.fmax = source_data->fmax;

    LOG_INFO("Iniciando relleno de huecos (interpolación de vecinos)...");
    DataF datagg_filled = dataf_copy(&datagg);

    // Cast pointers for the gap-filling loop
    float* datagg_original_data = (float*)datagg.data_in;
    float* datagg_filled_data = (float*)datagg_filled.data_in;

    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};
    const int num_neighbors = 4;

    #pragma omp parallel for collapse(2)
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t idx = y * width + x;
            if (isnan(datagg_original_data[idx])) {
                float sum = 0.0f;
                int count = 0;
                for (int k = 0; k < num_neighbors; k++) {
                    int nx = (int)x + dx[k];
                    int ny = (int)y + dy[k];
                    if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                        size_t n_idx = (size_t)ny * width + (size_t)nx;
                        float n_val = datagg_original_data[n_idx];
                        if (!isnan(n_val)) {
                            sum += n_val;
                            count++;
                        }
                    }
                }
                if (count > 0) {
                    datagg_filled_data[idx] = sum / (float)count;
                }
            }
        }
    }
    LOG_INFO("Relleno de huecos terminado.");

    // Liberar memoria intermedia
    dataf_destroy(&datagg);
    dataf_destroy(&navla);
    dataf_destroy(&navlo);

    // Devolvemos la malla rellena. El que llama es responsable de liberarla.
    return datagg_filled;
}