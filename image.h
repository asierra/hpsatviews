/* Image data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef _IMAGE_H_
#define _IMAGE_H_


// Estructura para guardan datos de una imagen
typedef struct {
  unsigned int width, height;
  unsigned int bpp; // Bytes per pixel, 1 = gray, 3 = rgb, 4 = rgba
  unsigned char *data;
} ImageData;

// Both images must be RGB and of the same size
ImageData blend_images(ImageData bg, ImageData fg, ImageData mask);

#endif
