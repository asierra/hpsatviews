/*
 * WebP Image Reader implementation for hpsatviews.
 * Optimized for RGB (3 channels) - No Alpha.
 *
 * Copyright (c) 2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
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
        LOG_ERROR("No se pudo abrir el archivo WebP: %s", filename);
        return image_create(0, 0, 0);
    }

    // 1. Obtener tamaño y leer archivo a memoria
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size == 0) {
        LOG_ERROR("Archivo WebP vacío: %s", filename);
        fclose(fp);
        return image_create(0, 0, 0);
    }

    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (!file_data) {
        LOG_FATAL("Falla de memoria al leer WebP comprimido.");
        fclose(fp);
        return image_create(0, 0, 0);
    }

    size_t bytes_read = fread(file_data, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != file_size) {
        LOG_ERROR("Error de lectura: %s", filename);
        free(file_data);
        return image_create(0, 0, 0);
    }

    // 2. Obtener dimensiones
    int width = 0, height = 0;
    if (!WebPGetInfo(file_data, file_size, &width, &height)) {
        LOG_ERROR("WebP inválido: %s", filename);
        free(file_data);
        return image_create(0, 0, 0);
    }

    // 3. Crear imagen de 3 canales (RGB)
    int bpp = 3; 
    ImageData image = image_create(width, height, bpp);

    if (image.data == NULL) {
        LOG_FATAL("Falla de memoria para buffer de imagen (%dx%d RGB).", width, height);
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
        LOG_ERROR("Error al decodificar WebP RGB: %s", filename);
        image_destroy(&image);
        return image_create(0, 0, 0);
    }

    LOG_INFO("WebP cargado (RGB): %s (%ux%u, 3 bpp)", filename, width, height);

    return image;
}