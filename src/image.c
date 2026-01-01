/* Image data structure and tools
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "image.h"
#include "datanc.h"
#include "logger.h"
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constructor for ImageData structure
ImageData image_create(unsigned int width, unsigned int height, unsigned int bpp) {
    ImageData image;

    // Initialize all fields
    image.width = width;
    image.height = height;
    image.bpp = bpp;
    image.data = NULL;

    // Validate bpp parameter
    if (bpp < 1 || bpp > 4) {
        image.width = 0;
        image.height = 0;
        image.bpp = 0;
        return image;
    }

    // Calculate total size needed
    size_t total_size = (size_t)width * height * bpp;

    // Allocate memory with error checking
    if (total_size > 0) {
        image.data = malloc(total_size);
        if (image.data == NULL) {
            // On allocation failure, reset all fields
            image.width = 0;
            image.height = 0;
            image.bpp = 0;
        }
    }

    return image;
}

// Destructor for ImageData structure
void image_destroy(ImageData *image) {
    if (image != NULL) {
        if (image->data != NULL) {
            free(image->data);
            image->data = NULL;
        }
        // Reset all fields to safe values
        image->width = 0;
        image->height = 0;
        image->bpp = 0;
    }
}

ImageData copy_image(ImageData orig) {
    size_t size = orig.width * orig.height;
    ImageData imout = image_create(orig.width, orig.height, orig.bpp);

    // Check if allocation was successful
    if (imout.data != NULL && orig.data != NULL) {
        memcpy(imout.data, orig.data, size * orig.bpp);
    }

    return imout;
}

/**
 * @brief Crops an image to a specified rectangular region.
 * @param src Pointer to the source image data.
 * @param x The starting x-coordinate of the crop rectangle.
 * @param y The starting y-coordinate of the crop rectangle.
 * @param width The width of the crop rectangle.
 * @param height The height of the crop rectangle.
 * @return A new ImageData structure containing the cropped image.
 *         The caller is responsible for freeing this new image.
 *         Returns an empty image on failure.
 */
ImageData image_crop(const ImageData *src, unsigned int x, unsigned int y, unsigned int width,
                     unsigned int height) {
    // Sanity checks
    if (src == NULL || src->data == NULL || width == 0 || height == 0) {
        return image_create(0, 0, 0);
    }
    if (x + width > src->width || y + height > src->height) {
        LOG_ERROR("El área de recorte excede las dimensiones de la imagen original.");
        return image_create(0, 0, 0);
    }

    ImageData cropped_img = image_create(width, height, src->bpp);
    if (cropped_img.data == NULL) {
        LOG_FATAL("Falla de memoria al crear la imagen recortada.");
        return cropped_img;
    }

    size_t src_row_stride = src->width * src->bpp;
    size_t cropped_row_stride = width * src->bpp;

#pragma omp parallel for
    for (unsigned int i = 0; i < height; ++i) {
        // Pointer to the start of the source row
        const unsigned char *src_row = src->data + (y + i) * src_row_stride + x * src->bpp;
        // Pointer to the start of the destination row
        unsigned char *dst_row = cropped_img.data + i * cropped_row_stride;
        memcpy(dst_row, src_row, cropped_row_stride);
    }

    return cropped_img;
}


ImageData blend_images(ImageData bg, ImageData fg, ImageData mask) {
    if (bg.width != fg.width || bg.height != fg.height || bg.width != mask.width ||
        bg.height != mask.height) {
        LOG_ERROR("Las dimensiones de las imágenes y la máscara no coinciden.");
        return image_create(0, 0, 0);
    }
 
    size_t size = bg.width * bg.height;
    ImageData imout = image_create(bg.width, bg.height, bg.bpp);

    if (imout.data == NULL) {
        return imout; // Return empty image on allocation failure
    }

    double start = omp_get_wtime();

#pragma omp parallel for shared(bg, fg, mask, imout)
    for (int i = 0; i < size; i++) {
        int p = i * bg.bpp;
        int pm = i * mask.bpp;

        float w = (float)(mask.data[pm] / 255.0);

        imout.data[p] = (unsigned char)(w * bg.data[p] + (1 - w) * fg.data[p]);
        imout.data[p + 1] = (unsigned char)(w * bg.data[p + 1] + (1 - w) * fg.data[p + 1]);
        imout.data[p + 2] = (unsigned char)(w * bg.data[p + 2] + (1 - w) * fg.data[p + 2]);
    }
    double end = omp_get_wtime();
    printf("Tiempo blend %lf\n", end - start);

    return imout;
}


static inline float luminance_from_rgb(uint8_t R, uint8_t G, uint8_t B) {
    return 0.2126f*R + 0.7152f*G + 0.0722f*B;
}


// ============================================================================
// Histogram Equalization (Global)
// ============================================================================

void image_apply_histogram(ImageData im) {
    size_t size = im.width * im.height;
    unsigned int histogram[256];

    for (unsigned int i = 0; i < 256; i++)
        histogram[i] = 0;

#pragma omp parallel for shared(im, histogram)
    for (unsigned int y = 0; y < im.height; y++) {
        for (unsigned int x = 0; x < im.width; x++) {
            unsigned int i = y * im.width + x;
            unsigned int po = i * im.bpp;
            unsigned int q;
            if (im.bpp >= 3)
                q = (unsigned int)(luminance_from_rgb(im.data[po],im.data[po + 1],im.data[po + 2])+0.5);
            else
                q = im.data[po];
            histogram[q]++;
        }
    }

    unsigned int cum = 0;
    unsigned char transfer[256]; // Función de transferencia
    for (unsigned int i = 0; i < 256; i++) {
        cum += histogram[i];
        transfer[i] = (unsigned char)(255.0 * cum / size);
    }

#pragma omp parallel for shared(im, transfer)
    for (size_t i = 0; i < size; i++) {
        unsigned int p = i * im.bpp;
        im.data[p] = transfer[im.data[p]];
        if (im.bpp >= 3) {
            im.data[p + 1] = transfer[im.data[p + 1]];
            im.data[p + 2] = transfer[im.data[p + 2]];
        }
    }
}

// ============================================================================
// CLAHE (Contrast Limited Adaptive Histogram Equalization) Implementation
// ============================================================================

#define CLAHE_NUM_BINS 256

ImageData extract_luminance_rgb(const ImageData *rgb) {
    if (rgb->bpp < 3) {
        LOG_ERROR("Solo se puede extraer luminancia de una imagen con 3 canales");
        return image_create(0, 0, 0);
    }
    ImageData lum = image_create(rgb->width, rgb->height, 1);
    if (lum.data == NULL) {
        LOG_ERROR("No se pudo asignar memoria para luminancia.");
        return image_create(0, 0, 0);
    }

    size_t size = rgb->width * rgb->height;
    #pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
		size_t po = i * rgb->bpp;
        uint8_t R = rgb->data[po];
        uint8_t G = rgb->data[po + 1];
        uint8_t B = rgb->data[po + 2];

        /* Luminancia lineal Rec.709 */
        float L = luminance_from_rgb(R, G, B);

        /* Clamp y cast */
        if (L < 0.0f)
            L = 0.0f;
        if (L > 255.0f)
            L = 255.0f;

        lum.data[i] = (uint8_t)(L + 0.5f);
    }
    return lum;
}

void apply_luminance_to_rgb(ImageData *rgb, const ImageData *lum_clahe) {
    if (rgb->bpp < 3) {
        LOG_ERROR("Solo se puede aplicar luminancia a una imagen RGB.");
        return;
    }
    size_t size = rgb->width * rgb->height;
    #pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
		size_t po = i * rgb->bpp;
		float r = rgb->data[po];
        float g = rgb->data[po + 1];
        float b = rgb->data[po + 2];

        float L0 =
            0.2126f * r +
            0.7152f * g +
            0.0722f * b;
        float L1 = lum_clahe->data[i];

        float ratio = L1 / (L0 + 1e-6f);

        /* Limitar ganancia extrema */
        if (ratio > 4.0f)
            ratio = 4.0f;

        r *= ratio;
        g *= ratio;
        b *= ratio;
        
        if (r > 255.0f) r = 255.0f;
        if (g > 255.0f) g = 255.0f;
        if (b > 255.0f) b = 255.0f;

        rgb->data[po]     = (uint8_t)(r + 0.5f);
        rgb->data[po + 1] = (uint8_t)(g + 0.5f);
        rgb->data[po + 2] = (uint8_t)(b + 0.5f);
    }
}

/**
 * @brief Recorta el histograma y redistribuye el exceso uniformemente.
 * @param hist Puntero al histograma (array de 256 enteros).
 * @param limit Límite máximo de píxeles permitidos por bin.
 */
static void clip_histogram(unsigned int *hist, unsigned int limit) {
    unsigned int excess = 0;

    // Paso 1: Calcular exceso y recortar
    for (int i = 0; i < CLAHE_NUM_BINS; i++) {
        if (hist[i] > limit) {
            excess += (hist[i] - limit);
            hist[i] = limit;
        }
    }
    // Paso 2: Redistribución uniforme
    unsigned int avg_inc = excess / CLAHE_NUM_BINS;
    unsigned int remainder = excess % CLAHE_NUM_BINS;

    if (avg_inc > 0) {
        for (int i = 0; i < CLAHE_NUM_BINS; i++) {
            hist[i] += avg_inc;
        }
    }
    // Paso 3: Redistribuir el remanente secuencialmente
    for (unsigned int i = 0; i < remainder; i++) {
        hist[i]++;
    }
}

/**
 * @brief Calcula el mapeo CDF (Cumulative Distribution Function) para un tile.
 * @param hist Histograma del tile.
 * @param map_lut Array de salida con el mapeo [0-255] -> [0-255].
 * @param pixels_per_tile Total de píxeles en el tile.
 */
static void calculate_cdf_mapping(unsigned int *hist, unsigned char *map_lut, int pixels_per_tile) {
    unsigned int sum = 0;
    float scale = 255.0f / pixels_per_tile;

    for (int i = 0; i < CLAHE_NUM_BINS; i++) {
        sum += hist[i];
        map_lut[i] = (unsigned char)(sum * scale + 0.5f);
    }
}

/**
 * @brief Aplica CLAHE (Contrast Limited Adaptive Histogram Equalization).
 * @param im Imagen a procesar (modificada in-place).
 * @param tiles_x Número de tiles horizontales (típicamente 8).
 * @param tiles_y Número de tiles verticales (típicamente 8).
 * @param clip_limit Factor de recorte (típicamente 2.0-4.0, mayor = más contraste).
 */
void image_apply_clahe(ImageData im, int tiles_x, int tiles_y, float clip_limit) {
    if (im.data == NULL || tiles_x < 1 || tiles_y < 1 || im.bpp < 1) {
        LOG_ERROR("Parámetros inválidos para CLAHE");
        return;
    }
	ImageData lum = (im.bpp < 3) ? im: extract_luminance_rgb(&im);

    int tile_width = im.width / tiles_x;
    int tile_height = im.height / tiles_y;
    int pixels_per_tile = tile_width * tile_height;

    // Calcular límite de recorte en píxeles
    unsigned int clip_limit_pixels =
        (unsigned int)((clip_limit * pixels_per_tile) / CLAHE_NUM_BINS);
    if (clip_limit_pixels < 1)
        clip_limit_pixels = 1;

    LOG_DEBUG("CLAHE: tiles=%dx%d, tile_size=%dx%d, clip_limit=%.2f (%u pixels)", tiles_x, tiles_y,
              tile_width, tile_height, clip_limit, clip_limit_pixels);

    unsigned char(*lut)[tiles_x][CLAHE_NUM_BINS] =
        malloc(sizeof(unsigned char[tiles_y][tiles_x][CLAHE_NUM_BINS]));

    if (lut == NULL) {
        LOG_ERROR("No se pudo asignar memoria para CLAHE LUTs");
        return;
    }

// Paso 1: Calcular LUTs para cada tile (paralelizable)
#pragma omp parallel for collapse(2)
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            unsigned int hist[CLAHE_NUM_BINS] = {0};

            // Definir límites del tile
            unsigned int x_start = tx * tile_width;
            unsigned int y_start = ty * tile_height;
            unsigned int x_end = (tx == tiles_x - 1) ? im.width : x_start + tile_width;
            unsigned int y_end = (ty == tiles_y - 1) ? im.height : y_start + tile_height;

            // Calcular histograma local
            for (unsigned int y = y_start; y < y_end; y++) {
                for (unsigned int x = x_start; x < x_end; x++) {
                    unsigned int idx = (y * lum.width + x) * lum.bpp;
                    hist[lum.data[idx]]++;
                }
            }

            // Recortar histograma
            clip_histogram(hist, clip_limit_pixels);

            // Calcular mapeo CDF y guardar en LUT
            int actual_pixels = (x_end - x_start) * (y_end - y_start);
            calculate_cdf_mapping(hist, lut[ty][tx], actual_pixels);
        }
    }
// Paso 2: Aplicar interpolación bilinear pixel por pixel
#pragma omp parallel for
    for (unsigned int y = 0; y < lum.height; y++) {
        for (unsigned int x = 0; x < lum.width; x++) {
            unsigned int idx = (y * lum.width + x) * lum.bpp;
            unsigned char pixel_val = lum.data[idx];

            // Calcular posición en el espacio de tiles (en coordenadas continuas)
            float fx = ((float)x / tile_width) - 0.5f;
            float fy = ((float)y / tile_height) - 0.5f;

            // Encontrar tiles vecinos
            int tx = (int)fx;
            int ty = (int)fy;

            // Clampear a límites válidos
            if (tx < 0)
                tx = 0;
            if (ty < 0)
                ty = 0;
            if (tx >= tiles_x - 1)
                tx = tiles_x - 2;
            if (ty >= tiles_y - 1)
                ty = tiles_y - 2;

            // Calcular coeficientes de interpolación
            float dx = fx - tx;
            float dy = fy - ty;

            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            if (dx > 1) dx = 1;
            if (dy > 1) dy = 1;

            // Obtener valores mapeados de los 4 tiles vecinos
            unsigned char val_tl = lut[ty][tx][pixel_val];         // Top-Left
            unsigned char val_tr = lut[ty][tx + 1][pixel_val];     // Top-Right
            unsigned char val_bl = lut[ty + 1][tx][pixel_val];     // Bottom-Left
            unsigned char val_br = lut[ty + 1][tx + 1][pixel_val]; // Bottom-Right

            // Interpolación bilinear
            float val_top = val_tl * (1.0f - dx) + val_tr * dx;
            float val_bot = val_bl * (1.0f - dx) + val_br * dx;
            float val_final = val_top * (1.0f - dy) + val_bot * dy;

            lum.data[idx] = (unsigned char)(val_final + 0.5f);
        }
    }
    free(lut);

	if (im.bpp >= 3) {
        apply_luminance_to_rgb(&im, &lum);
        image_destroy(&lum);
    }
    LOG_INFO("CLAHE aplicado: tiles=%dx%d, clip_limit=%.2f", tiles_x, tiles_y, clip_limit);
}


// ============================================================================
// Image Resampling
// ============================================================================

ImageData image_upsample_bilinear(const ImageData *src, int factor) {
    if (src == NULL || src->data == NULL || factor < 1) {
        return image_create(0, 0, 0);
    }

    unsigned int new_width = src->width * factor;
    unsigned int new_height = src->height * factor;
    ImageData result = image_create(new_width, new_height, src->bpp);

    if (result.data == NULL) {
        LOG_ERROR("No se pudo asignar memoria para el upsampling.");
        return result;
    }

    float xrat = (float)(src->width - 1) / (new_width - 1);
    float yrat = (float)(src->height - 1) / (new_height - 1);

    double start = omp_get_wtime();

#pragma omp parallel for
    for (unsigned int j = 0; j < new_height; j++) {
        for (unsigned int i = 0; i < new_width; i++) {
            float x = xrat * i;
            float y = yrat * j;
            int xl = (int)floor(x);
            int yl = (int)floor(y);
            int xh = (int)ceil(x);
            int yh = (int)ceil(y);
            float xw = x - xl;
            float yw = y - yl;

            int dst_idx = (j * new_width + i) * src->bpp;

            for (unsigned int ch = 0; ch < src->bpp; ch++) {
                int idx_ll = (yl * src->width + xl) * src->bpp + ch;
                int idx_lh = (yl * src->width + xh) * src->bpp + ch;
                int idx_hl = (yh * src->width + xl) * src->bpp + ch;
                int idx_hh = (yh * src->width + xh) * src->bpp + ch;

                double val = src->data[idx_ll] * (1 - xw) * (1 - yw) +
                             src->data[idx_lh] * xw * (1 - yw) + src->data[idx_hl] * (1 - xw) * yw +
                             src->data[idx_hh] * xw * yw;

                result.data[dst_idx + ch] = (unsigned char)(val + 0.5);
            }
        }
    }

    double end = omp_get_wtime();
    LOG_INFO("Upsampling bilinear (factor=%d): %.3f segundos", factor, end - start);

    return result;
}

ImageData image_downsample_boxfilter(const ImageData *src, int factor) {
    if (src == NULL || src->data == NULL || factor < 1) {
        return image_create(0, 0, 0);
    }

    unsigned int new_width = src->width / factor;
    unsigned int new_height = src->height / factor;

    if (new_width == 0 || new_height == 0) {
        LOG_ERROR("El factor de downsampling es demasiado grande para esta imagen.");
        return image_create(0, 0, 0);
    }

    ImageData result = image_create(new_width, new_height, src->bpp);

    if (result.data == NULL) {
        LOG_ERROR("No se pudo asignar memoria para el downsampling.");
        return result;
    }

    double start = omp_get_wtime();

#pragma omp parallel for
    for (unsigned int j = 0; j < new_height; j++) {
        for (unsigned int i = 0; i < new_width; i++) {
            unsigned int dst_idx = (j * new_width + i) * src->bpp;

            for (unsigned int ch = 0; ch < src->bpp; ch++) {
                double sum = 0.0;
                int count = 0;

                for (int dy = 0; dy < factor; dy++) {
                    for (int dx = 0; dx < factor; dx++) {
                        unsigned int src_x = i * factor + dx;
                        unsigned int src_y = j * factor + dy;

                        if (src_x < src->width && src_y < src->height) {
                            unsigned int src_idx = (src_y * src->width + src_x) * src->bpp + ch;
                            sum += src->data[src_idx];
                            count++;
                        }
                    }
                }

                result.data[dst_idx + ch] = (unsigned char)((sum / count) + 0.5);
            }
        }
    }

    double end = omp_get_wtime();
    LOG_INFO("Downsampling box filter (factor=%d): %.3f segundos", factor, end - start);

    return result;
}

ImageData image_create_alpha_mask_from_dataf(const void *data_ptr) {
    const DataF *data = (const DataF *)data_ptr;
    if (data == NULL || data->data_in == NULL) {
        return image_create(0, 0, 0);
    }

    // Crear una imagen de 1 canal (grayscale) para la máscara
    ImageData mask = image_create(data->width, data->height, 1);
    if (mask.data == NULL) {
        LOG_ERROR("No se pudo crear máscara alpha.");
        return mask;
    }

#pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        // 255 = opaco (dato válido), 0 = transparente (NonData)
        mask.data[i] = IS_NONDATA(data->data_in[i]) ? 0 : 255;
    }

    LOG_INFO("Máscara alpha creada: %ux%u", mask.width, mask.height);
    return mask;
}

ImageData image_add_alpha_channel(const ImageData *src, const ImageData *alpha_mask) {
    if (src == NULL || src->data == NULL || alpha_mask == NULL || alpha_mask->data == NULL) {
        return image_create(0, 0, 0);
    }

    if (src->width != alpha_mask->width || src->height != alpha_mask->height) {
        LOG_ERROR("Las dimensiones de la imagen y la máscara alpha no coinciden.");
        return image_create(0, 0, 0);
    }

    // Determinar nuevo bpp: 1->2 (gray+alpha), 3->4 (rgb+alpha)
    unsigned int new_bpp;
    if (src->bpp == 1) {
        new_bpp = 2;
    } else if (src->bpp == 3) {
        new_bpp = 4;
    } else {
        LOG_ERROR("Solo se puede agregar alpha a imágenes de 1 o 3 canales (bpp=%u).", src->bpp);
        return image_create(0, 0, 0);
    }

    ImageData result = image_create(src->width, src->height, new_bpp);
    if (result.data == NULL) {
        LOG_ERROR("No se pudo crear imagen con canal alpha.");
        return result;
    }

    size_t num_pixels = src->width * src->height;

#pragma omp parallel for
    for (size_t i = 0; i < num_pixels; i++) {
        size_t src_idx = i * src->bpp;
        size_t dst_idx = i * new_bpp;

        // Copiar canales originales
        for (unsigned int ch = 0; ch < src->bpp; ch++) {
            result.data[dst_idx + ch] = src->data[src_idx + ch];
        }

        // Agregar canal alpha de la máscara
        result.data[dst_idx + src->bpp] = alpha_mask->data[i];
    }

    LOG_INFO("Canal alpha agregado: %ux%u, bpp %u->%u", result.width, result.height, src->bpp,
             new_bpp);
    return result;
}

ImageData image_expand_palette(const ImageData *src, const ColorArray *palette) {
    if (!src || !palette) {
        LOG_ERROR("Parámetros inválidos para image_expand_palette.");
        return image_create(0, 0, 0);
    }

    // Solo soportamos bpp=1 (indexed) o bpp=2 (indexed+alpha)
    if (src->bpp != 1 && src->bpp != 2) {
        LOG_ERROR("image_expand_palette solo acepta bpp=1 o bpp=2 (recibido: %u)", src->bpp);
        return image_create(0, 0, 0);
    }

    // Si tiene alpha (bpp=2), salida será RGBA (bpp=4); si no RGB (bpp=3)
    unsigned int out_bpp = (src->bpp == 2) ? 4 : 3;
    ImageData result = image_create(src->width, src->height, out_bpp);
    if (result.data == NULL) {
        LOG_ERROR("No se pudo crear imagen expandida.");
        return result;
    }

    size_t num_pixels = src->width * src->height;

#pragma omp parallel for
    for (size_t i = 0; i < num_pixels; i++) {
        size_t src_idx = i * src->bpp;
        size_t dst_idx = i * out_bpp;

        uint8_t index = src->data[src_idx];

        // Expandir índice a RGB usando la paleta
        if (index < palette->length) {
            result.data[dst_idx + 0] = palette->colors[index].r;
            result.data[dst_idx + 1] = palette->colors[index].g;
            result.data[dst_idx + 2] = palette->colors[index].b;
        } else {
            // Índice fuera de rango -> negro
            result.data[dst_idx + 0] = 0;
            result.data[dst_idx + 1] = 0;
            result.data[dst_idx + 2] = 0;
        }

        // Si la fuente tiene alpha, copiarlo
        if (src->bpp == 2) {
            result.data[dst_idx + 3] = src->data[src_idx + 1];
        }
    }

    LOG_INFO("Imagen expandida de paleta: %ux%u, bpp %u->%u", result.width, result.height, src->bpp,
             out_bpp);
    return result;
}
