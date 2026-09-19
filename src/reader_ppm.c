/* Binary PPM (P6) image reader (city-lights background layers).
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "image.h"
#include "logger.h"
#include "reader_ppm.h"

/* Siguiente entero de la cabecera, saltando espacios y comentarios '#'. */
static int ppm_read_uint(FILE *fp, unsigned int *out) {
    int c = fgetc(fp);
    for (;;) {
        while (c != EOF && isspace(c)) c = fgetc(fp);
        if (c != '#') break;
        while (c != EOF && c != '\n') c = fgetc(fp);
    }
    if (c == EOF || !isdigit(c)) return -1;
    unsigned long v = 0;
    while (c != EOF && isdigit(c)) {
        v = v * 10 + (unsigned long)(c - '0');
        if (v > 100000) return -1;
        c = fgetc(fp);
    }
    /* El único espacio que sigue al maxval separa la cabecera de los datos, así
     * que no se devuelve al flujo. */
    if (c == EOF || !isspace(c)) return -1;
    *out = (unsigned int)v;
    return 0;
}

ImageData reader_load_ppm(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        LOG_ERROR("Could not open PPM file: %s", filename);
        return image_create(0, 0, 0);
    }

    unsigned int width = 0, height = 0, maxval = 0;
    if (fgetc(fp) != 'P' || fgetc(fp) != '6' ||
        ppm_read_uint(fp, &width) || ppm_read_uint(fp, &height) ||
        ppm_read_uint(fp, &maxval) || width == 0 || height == 0) {
        LOG_ERROR("Not a binary PPM (P6): %s", filename);
        fclose(fp);
        return image_create(0, 0, 0);
    }
    if (maxval != 255) {
        LOG_ERROR("PPM with maxval %u is not supported (8-bit only): %s", maxval, filename);
        fclose(fp);
        return image_create(0, 0, 0);
    }

    ImageData image = image_create(width, height, 3);
    if (image.data == NULL) {
        LOG_FATAL("Memory allocation failed for image buffer (%ux%u RGB).", width, height);
        fclose(fp);
        return image;
    }

    size_t n = (size_t)width * height * 3;
    size_t bytes_read = fread(image.data, 1, n, fp);
    fclose(fp);
    if (bytes_read != n) {
        LOG_ERROR("Truncated PPM: %s (%zu of %zu bytes)", filename, bytes_read, n);
        image_destroy(&image);
        return image_create(0, 0, 0);
    }

    LOG_INFO("PPM loaded (RGB): %s (%ux%u, 3 bpp)", filename, width, height);
    return image;
}
