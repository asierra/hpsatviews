/*
 * A simple libpng example program
 * http://zarb.org/~gc/html/libpng.html
 *
 * Modified by Yoshimasa Niwa to make it much simpler
 * and support all defined color_type.
 *
 * Refactored for hpsatviews by Alejandro Aguilar Sierra.
 *
 * Copyright 2002-2010 Guillaume Cottenceau.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <png.h>
#include <string.h>

#include "image.h"
#include "logger.h"

/**
 * @brief Lee un archivo PNG y lo carga en una estructura ImageData.
 * 
 * Esta función maneja diferentes tipos de color y profundidades de bit,
 * convirtiéndolos a un formato estándar RGB (3 canales) o RGBA (4 canales) de 8 bits.
 * 
 * @param filename La ruta al archivo PNG.
 * @return Una estructura ImageData con los datos de la imagen. Si falla,
 *         la estructura devuelta tendrá su puntero `data` a NULL.
 */
ImageData reader_load_png(const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    LOG_ERROR("No se pudo abrir el archivo PNG: %s", filename);
    return image_create(0, 0, 0);
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) {
    LOG_ERROR("png_create_read_struct falló.");
    fclose(fp);
    return image_create(0, 0, 0);
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    LOG_ERROR("png_create_info_struct falló.");
    png_destroy_read_struct(&png, NULL, NULL);
    fclose(fp);
    return image_create(0, 0, 0);
  }

  if (setjmp(png_jmpbuf(png))) {
    LOG_ERROR("Error durante la inicialización de I/O de libpng.");
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return image_create(0, 0, 0);
  }

  png_init_io(png, fp);
  png_read_info(png, info);

  unsigned int width = png_get_image_width(png, info);
  unsigned int height = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  // --- Transformaciones para estandarizar el formato de salida ---
  // Convertir a 8 bits por canal si es de 16.
  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  // Expandir paleta a RGB.
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  // Expandir escala de grises de menos de 8 bits a 8 bits.
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  // Si hay información de transparencia (chunk tRNS), convertirla a un canal alfa completo.
  if (png_get_valid(png, info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png);
  }
  // Si no hay canal alfa, añadir uno opaco (255). Esto asegura salida RGBA.
  if (color_type == PNG_COLOR_TYPE_RGB ||
      color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  }
  // Convertir escala de grises (con o sin alfa) a RGB.
  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }

  png_read_update_info(png, info);

  // --- Crear la estructura ImageData y leer la imagen ---
  unsigned int bpp = png_get_channels(png, info);
  ImageData image = image_create(width, height, bpp);
  if (image.data == NULL) {
    LOG_FATAL("Falla de memoria al crear buffer para imagen PNG.");
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return image; // Devuelve imagen vacía
  }

  // Crear un array de punteros a las filas de nuestra imagen final.
  png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
  if (row_pointers == NULL) {
    LOG_FATAL("Falla de memoria para los punteros de fila de PNG.");
    image_destroy(&image);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return image_create(0, 0, 0);
  }

  for (unsigned int y = 0; y < height; y++) {
    row_pointers[y] = image.data + (y * width * bpp);
  }

  // Leer la imagen directamente en el buffer de ImageData.
  png_read_image(png, row_pointers);

  fclose(fp);
  free(row_pointers);
  png_destroy_read_struct(&png, &info, NULL);

  LOG_INFO("PNG cargado: %s (%ux%u, %d bpp)", filename, width, height, bpp);

  return image;
}
