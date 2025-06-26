/* Image data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef _IMAGE_H_
#define _IMAGE_H_


// Estructura para guardar datos de una imagen
typedef struct {
  unsigned int width, height;
  unsigned int bpp; // Bytes per pixel: 1 = gray, 2 = gray+a, 3 = rgb, 4 = rgba
  unsigned char *data;
} ImageData;


// Creates a new image from an original image.
ImageData copy_image(ImageData orig);

// Both images must be RGB and of the same size
ImageData blend_images(ImageData bg, ImageData fg, ImageData mask);

// Apply histogram enhacement to image
void image_apply_histogram(ImageData im);

// Apply gamma correction to image
void image_apply_gamma(ImageData im, float gamma);

#endif
