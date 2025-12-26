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
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "image.h"

/**
 * @brief Función interna para escribir datos de imagen a un archivo PNG.
 * 
 * Esta es la función principal que interactúa con libpng.
 * 
 * @param filename Ruta del archivo.
 * @param image Puntero a la imagen a guardar.
 * @param color_type Tipo de color de PNG (ej. PNG_COLOR_TYPE_RGB).
 * @param palette Puntero a la paleta de colores (solo para PNG_COLOR_TYPE_PALETTE).
 * @param transp Puntero al array de transparencia (solo para PNG_COLOR_TYPE_PALETTE).
 * @return 0 en éxito, 1 en error.
 */
static int write_png_core(const char *filename, const ImageData *image, png_byte color_type,
                          const ColorArray *palette, const png_byte *transp) {
  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    LOG_ERROR("No se pudo abrir el archivo PNG para escritura: %s", filename);
    return 1;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) {
    LOG_ERROR("png_create_write_struct falló.");
    fclose(fp);
    return 1;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    LOG_ERROR("png_create_info_struct falló.");
    png_destroy_write_struct(&png, NULL);
    fclose(fp);
    return 1;
  }

  if (setjmp(png_jmpbuf(png))) {
    LOG_ERROR("Error durante la inicialización de I/O de libpng.");
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 1;
  }

  png_init_io(png, fp);

  // Escribir el header del PNG. Asumimos siempre 8 bits de profundidad.
  png_set_IHDR(png, info, image->width, image->height, 8, color_type,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  // Si es una imagen con paleta, escribir los chunks PLTE y tRNS.
  if (color_type == PNG_COLOR_TYPE_PALETTE && palette) {
    png_set_PLTE(png, info, (png_colorp)palette->colors, palette->length);
    if (transp) {
      png_set_tRNS(png, info, (png_bytep)transp, palette->length, NULL);
    }
  }

  png_write_info(png, info);

  // Crear un array de punteros a las filas de la imagen.
  // Esto no copia los datos, solo crea punteros.
  png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * image->height);
  if (!row_pointers) {
    LOG_FATAL("Falla de memoria para los punteros de fila de PNG.");
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 1;
  }

  for (unsigned int y = 0; y < image->height; y++) {
    row_pointers[y] = image->data + (y * image->width * image->bpp);
  }

  // Escribir los datos de la imagen.
  png_write_image(png, row_pointers);
  png_write_end(png, NULL);

  // Limpieza.
  free(row_pointers);
  png_destroy_write_struct(&png, &info);
  fclose(fp);

  LOG_INFO("PNG guardado: %s (%ux%u, %u bpp)", filename, image->width, image->height, image->bpp);
  return 0;
}

int writer_save_png_palette(const char *filename, const ImageData *image, const ColorArray *palette) {
  if (image->bpp != 1 && image->bpp != 2) {
    LOG_ERROR("writer_save_png_palette solo acepta bpp=1 o bpp=2 (recibido: %u)", image->bpp);
    return 1;
  }
  if (!palette || palette->length == 0) {
    LOG_ERROR("Se requiere una paleta válida para guardar una imagen con paleta.");
    return 1;
  }

  png_byte *transp = NULL;
  ImageData image_to_write = *image; // Por defecto, usar la imagen original.
  ImageData temp_image = {0}; // Imagen temporal si bpp=2

  // Si bpp=2, la imagen es [índice, alfa]. Necesitamos extraer el canal alfa
  // al chunk tRNS y crear una imagen temporal de bpp=1 solo con los índices.
  if (image->bpp == 2) {
    transp = (png_byte*)calloc(palette->length, sizeof(png_byte));
    if (!transp) {
      LOG_FATAL("Falla de memoria al crear buffer de transparencia.");
      return 1;
    }
    // Inicializar todos los valores de transparencia a opaco (255).
    memset(transp, 255, palette->length * sizeof(png_byte));

    // Crear una imagen temporal de 1 bpp para los índices.
    temp_image = image_create(image->width, image->height, 1);
    if (!temp_image.data) {
      LOG_FATAL("Falla de memoria al crear imagen temporal para índices.");
      free(transp);
      return 1;
    }

    // Recorrer la imagen original para separar índices y alfa.
    for (size_t i = 0; i < image->width * image->height; ++i) {
      uint8_t palette_idx = image->data[i * 2];
      uint8_t alpha = image->data[i * 2 + 1];
      
      temp_image.data[i] = palette_idx; // Guardar solo el índice.

      // Guardar el valor de alfa más bajo encontrado para cada índice de la paleta.
      if (palette_idx < palette->length && alpha < transp[palette_idx]) {
        transp[palette_idx] = alpha;
      }
    }
    image_to_write = temp_image; // Apuntar a la imagen temporal para la escritura.
  }

  int result = write_png_core(filename, &image_to_write, PNG_COLOR_TYPE_PALETTE, palette, transp);

  // Limpiar memoria temporal si fue usada.
  if (transp) {
    free(transp);
  }
  if (temp_image.data) {
    image_destroy(&temp_image);
  }

  return result;
}

int writer_save_png(const char *filename, const ImageData *image) {
  png_byte color_type;

  switch (image->bpp) {
    case 1:
      color_type = PNG_COLOR_TYPE_GRAY;
      break;
    case 2:
      color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
      break;
    case 3:
      color_type = PNG_COLOR_TYPE_RGB;
      break;
    case 4:
      color_type = PNG_COLOR_TYPE_RGB_ALPHA;
      break;
    default:
      LOG_ERROR("BPP no soportado para escritura PNG: %u. Soportados: 1, 2, 3, 4.", image->bpp);
      return 1;
  }

  return write_png_core(filename, image, color_type, NULL, NULL);
}

/* --- Funciones antiguas, mantenidas por compatibilidad pero marcadas como obsoletas --- */

/** @deprecated Usar writer_save_png_palette en su lugar. */
int write_image_png_palette(const char *filename, ImageData *image, ColorArray *palette) {
    return writer_save_png_palette(filename, image, palette);
}

/** @deprecated Usar writer_save_png en su lugar. */
int write_image_png(const char *filename, ImageData *image) {
    return writer_save_png(filename, image);
}
