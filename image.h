/* Image data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_IMAGE_H_
#define HPSATVIEWS_IMAGE_H_

#include <stdint.h>
#include <stdlib.h>

// Estructura para guardar datos de una imagen
typedef struct {
  unsigned int width, height;
  unsigned int bpp; // Bytes per pixel: 1 = gray, 2 = gray+a, 3 = rgb, 4 = rgba
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
  color_array = malloc(sizeof(ColorArray) + sizeof(Color) * size);
  color_array->length = size;
  return color_array;
}

static inline void color_array_destroy(ColorArray *array) {
  if (array)
    free(array);
}

// Constructor: creates a new ImageData structure with allocated memory
// Returns initialized ImageData on success, or ImageData with NULL data on failure
ImageData image_create(unsigned int width, unsigned int height, unsigned int bpp);

// Destructor: safely frees memory allocated for ImageData
// Safe to call with NULL data pointer
void image_destroy(ImageData *image);

// Creates a new image from an original image.
ImageData copy_image(ImageData orig);

// Both images must be RGB and of the same size
ImageData blend_images(ImageData bg, ImageData fg, ImageData mask);

// Apply histogram enhacement to image
void image_apply_histogram(ImageData im);

// Apply gamma correction to image
void image_apply_gamma(ImageData im, float gamma);

#endif /* HPSATVIEWS_IMAGE_H_ */
