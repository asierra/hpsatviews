/* Byte Array Data Structure and tools
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_IMAGE_H_
#define HPSATVIEWS_IMAGE_H_

#include <stdint.h>
#include <stdlib.h>

/// 8-bit raster buffer for grayscale or RGB imagery.
typedef struct {
  unsigned int width, height;
  unsigned int bpp; ///< Bytes per pixel: 1 = gray, 2 = gray+a, 3 = rgb, 4 = rgba
  unsigned char *data;
} ImageData;

typedef struct {
    uint8_t r, g, b;
} Color;

typedef struct {
    unsigned length;
    Color colors[]; 
} ColorArray;

typedef struct {
    unsigned length;
    uint8_t transp[]; 
} TranspArray;

static inline ColorArray *color_array_create(unsigned size) {
  ColorArray *color_array;
  /* Cast requerido: este header también se compila como C++ (nvcc, vía
   * cuda_kernels.h), donde void* no convierte implícitamente. */
  color_array = (ColorArray *)malloc(sizeof(ColorArray) + sizeof(Color) * size);
  color_array->length = size;
  return color_array;
}

static inline void color_array_destroy(ColorArray *array) {
  if (array)
    free(array);
}

/// Allocates an ImageData buffer.
ImageData image_create(unsigned int width, unsigned int height, unsigned int bpp);

/// Frees an ImageData buffer.
void image_destroy(ImageData *image);

/// Creates a deep copy of an ImageData buffer.
ImageData copy_image(ImageData orig);

/// Extracts a rectangular subimage.
ImageData image_crop(const ImageData* src, unsigned int x, unsigned int y, unsigned int width, unsigned int height);

/// Alpha-blends fg over bg using a grayscale mask.
ImageData blend_images(ImageData bg, ImageData fg, ImageData mask);

/// Global histogram equalization.
void image_apply_histogram(ImageData im);

/**
 * CLAHE (Contrast Limited Adaptive Histogram Equalization), in-place.
 * 
 * @param tiles_x Grid of contextual regions (typically 8×8).
 * @param tiles_y Grid of contextual regions (typically 8×8).
 * @param clip_limit Redistribution threshold (2.0–4.0 typical).
 */
void image_apply_clahe(ImageData im, int tiles_x, int tiles_y, float clip_limit);

/// Bilinear interpolation upsampling by integer factor.
ImageData image_upsample_bilinear(const ImageData* src, int factor);

/// Box-filter (averaging) downsampling by integer factor.
ImageData image_downsample_boxfilter(const ImageData* src, int factor);

/// Generates a single-channel validity mask from a DataF.
ImageData image_create_alpha_mask_from_dataf(const void* data);

/// Appends an alpha channel using a mask image.
ImageData image_add_alpha_channel(const ImageData* src, const ImageData* alpha_mask);

/// Maps an indexed or indexed+alpha image to RGB/RGBA via a color palette.
ImageData image_expand_palette(const ImageData* src, const ColorArray* palette);

#endif /* HPSATVIEWS_IMAGE_H_ */
