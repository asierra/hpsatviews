#ifndef _IMAGE_H_
#define _IMAGE_H_


// Estructura para guardan datos de una imagen
typedef struct {
  unsigned int width, height;
  unsigned int bpp; // Bytes per pixel, 1 = gray, 3 = rgb, 4 = rgba
  unsigned char *data;
} ImageData;


#endif
