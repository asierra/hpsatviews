/* WebP image reader (city-lights background layers).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <webp/decode.h>

#include "image.h"
#include "logger.h"
#include "reader_webp.h"

ImageData reader_load_webp(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        LOG_ERROR("Could not open WebP file: %s", filename);
        return image_create(0, 0, 0);
    }

    // Get file size and read into memory.
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size == 0) {
        LOG_ERROR("Empty WebP file: %s", filename);
        fclose(fp);
        return image_create(0, 0, 0);
    }

    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (!file_data) {
        LOG_FATAL("Memory allocation failed while reading compressed WebP.");
        fclose(fp);
        return image_create(0, 0, 0);
    }

    size_t bytes_read = fread(file_data, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != file_size) {
        LOG_ERROR("Read error: %s", filename);
        free(file_data);
        return image_create(0, 0, 0);
    }

    // 2. Obtener dimensiones
    int width = 0, height = 0;
    if (!WebPGetInfo(file_data, file_size, &width, &height)) {
        LOG_ERROR("Invalid WebP: %s", filename);
        free(file_data);
        return image_create(0, 0, 0);
    }

    // 3. Crear imagen de 3 canales (RGB)
    int bpp = 3; 
    ImageData image = image_create(width, height, bpp);

    if (image.data == NULL) {
        LOG_FATAL("Memory allocation failed for image buffer (%dx%d RGB).", width, height);
        free(file_data);
        return image;
    }

    // 4. Decodificar como RGB
    // Stride = ancho * 3 bytes
    int stride = width * 3;
    size_t output_buffer_size = (size_t)stride * height;

    // Usamos WebPDecodeRGBInto en lugar de RGBAInto
    uint8_t *result = WebPDecodeRGBInto(file_data, file_size, 
                                        image.data, output_buffer_size, stride);

    free(file_data); // Liberamos el comprimido

    if (result == NULL) {
        LOG_ERROR("Error decoding WebP RGB: %s", filename);
        image_destroy(&image);
        return image_create(0, 0, 0);
    }

    LOG_INFO("WebP loaded (RGB): %s (%ux%u, 3 bpp)", filename, width, height);

    return image;
}