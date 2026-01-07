/* True color RGB image generation
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>

#include "truecolor.h"
#include "logger.h"
#include "rayleigh.h"
#include "reader_nc.h"
#include "rgb.h"


DataF create_truecolor_synthetic_green(const DataF *c_blue, const DataF *c_red, const DataF *c_nir) {
    // Validar dimensiones
    if (c_blue->width != c_red->width || c_blue->height != c_red->height) {
        LOG_ERROR("Dimension mismatch in TrueColor generation");
        return dataf_create(0, 0);
    }

    DataF green = dataf_create(c_red->width, c_red->height);
    if (green.data_in == NULL) return green;

    // Inicializar min/max invertidos para búsqueda
    green.fmin = NonData; 
    green.fmax = -NonData;

    float local_min = 1e30f;
    float local_max = -1e30f;

    #pragma omp parallel for reduction(min:local_min) reduction(max:local_max)
    for (size_t i = 0; i < green.size; i++) {
        float B = c_blue->data_in[i];
        float R = c_red->data_in[i];
        float N = c_nir->data_in[i];

        // Si alguno es NonData, el resultado es NonData
        if (IS_NONDATA(B) || IS_NONDATA(R) || IS_NONDATA(N)) {
            green.data_in[i] = NonData;
        } else {
            // Cálculo lineal físico (sin gamma ni clips aún)
            //float G_val = //(0.45f * R) + (0.10f * N) + (0.45f * B);
			//	0.45706946f * B + 0.48358168f * R + 0.06038137f * N;
			float G_val = (0.48358168f * R) + (0.45706946f * B) + (0.05934885f * N);
            green.data_in[i] = G_val;

            if (G_val < local_min) local_min = G_val;
            if (G_val > local_max) local_max = G_val;
        }
    }
    
    // Si no encontramos datos válidos, dejar min/max por defecto
    if (local_min < 1e29f) {
        green.fmin = local_min;
        green.fmax = local_max;
    }
    
    return green;
}


ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max) {
    if (!r_ch || !g_ch || !b_ch || !r_ch->data_in || !g_ch->data_in || !b_ch->data_in) {
        LOG_ERROR("Invalid input channels for create_multiband_rgb");
        return image_create(0, 0, 0);
    }

    if (r_ch->width != g_ch->width || r_ch->height != g_ch->height ||
        r_ch->width != b_ch->width || r_ch->height != b_ch->height) {
        LOG_ERROR("Channel dimensions mismatch in create_multiband_rgb");
        return image_create(0, 0, 0);
    }

    ImageData imout = image_create(r_ch->width, r_ch->height, 3);
    if (imout.data == NULL) {
        LOG_ERROR("Memory allocation failed for output image");
        return image_create(0, 0, 0);
    }

    size_t size = r_ch->size;
    float r_range = r_max - r_min;
    float g_range = g_max - g_min;
    float b_range = b_max - b_min;

    // Avoid division by zero
    if (fabs(r_range) < 1e-6) r_range = 1.0f;
    if (fabs(g_range) < 1e-6) g_range = 1.0f;
    if (fabs(b_range) < 1e-6) b_range = 1.0f;

    #pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
        float r_val = r_ch->data_in[i];
        float g_val = g_ch->data_in[i];
        float b_val = b_ch->data_in[i];

        uint8_t r_byte = 0, g_byte = 0, b_byte = 0;

        if (!IS_NONDATA(r_val)) {
            float norm = (r_val - r_min) / r_range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            r_byte = (uint8_t)(norm * 255.0f);
        }

        if (!IS_NONDATA(g_val)) {
            float norm = (g_val - g_min) / g_range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            g_byte = (uint8_t)(norm * 255.0f);
        }

        if (!IS_NONDATA(b_val)) {
            float norm = (b_val - b_min) / b_range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            b_byte = (uint8_t)(norm * 255.0f);
        }

        size_t idx = i * 3;
        imout.data[idx] = r_byte;
        imout.data[idx + 1] = g_byte;
        imout.data[idx + 2] = b_byte;
    }

    return imout;
}
