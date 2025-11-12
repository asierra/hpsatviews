/*
 * A simple libpng example program
 * http://zarb.org/~gc/html/libpng.html
 *
 * Modified by Yoshimasa Niwa to make it much simpler
 * and support all defined color_type.
 *
 * Simplified for particular use by Alejandro Aguilar Sierra.
 *
 * Copyright 2002-2010 Guillaume Cottenceau.
 *
 * This software may be freely redistributed under the terms
 * of the X11 license.
 *
 */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#include "image.h"


typedef struct {
  int width, height;
  png_byte color_type;
  png_byte bit_depth;
  png_bytep *row_pointers;
  png_color* palette;
  unsigned palette_size;
} png_t;

void write_png_file(const char *filename, png_t *pngt);

int write_image_png_palette(const char *filename, ImageData *image, ColorArray *palette) {
  if (image->bpp > 1) {
    LOG_ERROR("Solo imágenes monocromáticas sin alpha pueden tener paleta.");
    return 1; 
  }
  int bpp = image->bpp;
  png_t mipng;
  mipng.width = image->width;
  mipng.height = image->height;
  mipng.color_type = PNG_COLOR_TYPE_PALETTE;
  mipng.bit_depth = 8;
  mipng.row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * mipng.height);
  for (int y = 0; y < mipng.height; y++) {
    int p = y * mipng.width * bpp;
    mipng.row_pointers[y] = &(image->data[p]);
  }
  mipng.palette = (struct png_color_struct *)&palette->colors;
  mipng.palette_size = palette->length;
  write_png_file(filename, &mipng);

  return 0;
}


int write_image_png(const char *filename, ImageData *image) {
  int bpp = image->bpp;
  png_t mipng;

  mipng.width = image->width;
  mipng.height = image->height;
  if (bpp == 1)
    mipng.color_type = PNG_COLOR_TYPE_GRAY;
  if (bpp == 2)
    mipng.color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
  else if (bpp == 3)
    mipng.color_type = PNG_COLOR_TYPE_RGB;
  else if (bpp == 4)
    mipng.color_type = PNG_COLOR_TYPE_RGB_ALPHA;

  mipng.bit_depth = 8;
  mipng.row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * mipng.height);
  for (int y = 0; y < mipng.height; y++) {
    int p = y * mipng.width * bpp;
    mipng.row_pointers[y] = &(image->data[p]);
  }
  write_png_file(filename, &mipng);

  return 0;
}

void write_png_file(const char *filename, png_t *pngt) {
  FILE *fp = fopen(filename, "wb");
  if (!fp)
    abort();

  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    abort();

  png_infop info = png_create_info_struct(png);
  if (!info)
    abort();

  if (setjmp(png_jmpbuf(png)))
    abort();

  png_init_io(png, fp);

  // Output is always 8bit depth
  png_set_IHDR(png, info, pngt->width, pngt->height, 8, pngt->color_type,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  if(pngt->color_type==PNG_COLOR_TYPE_PALETTE) {
    png_set_PLTE(png, info, pngt->palette, pngt->palette_size);
  }

  png_write_info(png, info);

  // To remove the alpha channel for PNG_COLOR_TYPE_RGB format,
  // Use png_set_filler().
  // png_set_filler(png, 0, PNG_FILLER_AFTER);

  png_write_image(png, pngt->row_pointers);
  png_write_end(png, NULL);

  /*
  for(int y = 0; y < pngt->height; y++) {
    free(pngt->row_pointers[y]);
  }
  free(pngt->row_pointers);
  */
  fclose(fp);
}
