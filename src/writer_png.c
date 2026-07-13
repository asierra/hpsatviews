/* PNG output writer (direct and palette-indexed) via libpng.
 * Copyright 2002-2010 Guillaume Cottenceau.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
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
    LOG_ERROR("Could not open PNG file for writing: %s", filename);
    return 1;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) {
    LOG_ERROR("png_create_write_struct failed.");
    fclose(fp);
    return 1;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    LOG_ERROR("png_create_info_struct failed.");
    png_destroy_write_struct(&png, NULL);
    fclose(fp);
    return 1;
  }

  if (setjmp(png_jmpbuf(png))) {
    LOG_ERROR("Error during libpng I/O initialization.");
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 1;
  }

  png_init_io(png, fp);

  // Encoding speed: libpng's defaults (zlib level 6 + adaptive filtering, which
  // tries all 5 filters per row) dominate the wall-time on large full-disk
  // images (~21 s for 10848²×3). Benchmarks on real GOES imagery (high entropy)
  // show that level 1 + a single cheap filter writes ~2.3× faster (~21 s → ~4 s)
  // for a modest +6% file size; higher levels barely shrink the output but cost
  // far more. Pixels are unchanged — this only trades compressed size for speed.
  // The SUB filter (predict from the left neighbour) compresses continuous-tone
  // imagery well at low cost, but for palette images filtering the colour
  // indices is counter-productive (and discouraged by the spec), so use NONE.
  png_set_compression_level(png, 1);
  png_set_filter(png, 0,
                 color_type == PNG_COLOR_TYPE_PALETTE ? PNG_FILTER_NONE : PNG_FILTER_SUB);

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
    LOG_FATAL("Memory allocation failed for PNG row pointers.");
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

  LOG_INFO("PNG saved: %s (%ux%u, %u bpp)", filename, image->width, image->height, image->bpp);
  return 0;
}

int writer_save_png_palette(const char *filename, const ImageData *image, const ColorArray *palette) {
  if (image->bpp != 1 && image->bpp != 2) {
    LOG_ERROR("writer_save_png_palette only accepts bpp=1 or bpp=2 (got: %u)", image->bpp);
    return 1;
  }
  if (!palette || palette->length == 0) {
    LOG_ERROR("A valid palette is required to save a paletted image.");
    return 1;
  }

  // libpng exige que el PLTE tenga exactamente 2, 4, 16 o 256 entradas
  // (las únicas profundidades de bit válidas para PNG_COLOR_TYPE_PALETTE).
  // Si la paleta recibida no calza, se rellena hasta el siguiente tamaño válido.
  unsigned int padded_size;
  if (palette->length <= 2) padded_size = 2;
  else if (palette->length <= 4) padded_size = 4;
  else if (palette->length <= 16) padded_size = 16;
  else padded_size = 256;

  ColorArray *padded_palette = NULL;
  if (padded_size != palette->length) {
    padded_palette = color_array_create(padded_size);
    if (!padded_palette) {
      LOG_FATAL("Memory allocation failed while resizing palette.");
      return 1;
    }
    memcpy(padded_palette->colors, palette->colors, sizeof(Color) * palette->length);
    Color fill_color = palette->colors[palette->length - 1];
    for (unsigned int i = palette->length; i < padded_size; i++) {
      padded_palette->colors[i] = fill_color;
    }
    palette = padded_palette;
  }

  png_byte *transp = NULL;
  ImageData image_to_write = *image;
  ImageData temp_image = {0};

  // If bpp=2 the image is [palette_index, alpha]. Extract the alpha channel
  // into the tRNS chunk and build a bpp=1 index-only image for libpng.
  if (image->bpp == 2) {
    transp = (png_byte*)calloc(palette->length, sizeof(png_byte));
    if (!transp) {
      LOG_FATAL("Memory allocation failed for transparency buffer.");
      if (padded_palette) color_array_destroy(padded_palette);
      return 1;
    }
    // Inicializar todos los valores de transparencia a opaco (255).
    memset(transp, 255, palette->length * sizeof(png_byte));

    // Build a bpp=1 index-only image.
    temp_image = image_create(image->width, image->height, 1);
    if (!temp_image.data) {
      LOG_FATAL("Memory allocation failed for temporary index image.");
      free(transp);
      if (padded_palette) color_array_destroy(padded_palette);
      return 1;
    }

    // Separate indices and alpha values from the packed bpp=2 image.
    for (size_t i = 0; i < image->width * image->height; ++i) {
      uint8_t palette_idx = image->data[i * 2];
      uint8_t alpha = image->data[i * 2 + 1];
      
      temp_image.data[i] = palette_idx;

      // Record the minimum alpha found for each palette entry (tRNS).
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
  if (padded_palette) {
    color_array_destroy(padded_palette);
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
      LOG_ERROR("Unsupported BPP for PNG writing: %u. Supported: 1, 2, 3, 4.", image->bpp);
      return 1;
  }

  return write_png_core(filename, image, color_type, NULL, NULL);
}

/* --- Funciones antiguas, mantenidas por compatibilidad pero marcadas como obsoletas --- */

int write_image_png_palette(const char *filename, ImageData *image, ColorArray *palette) {
    return writer_save_png_palette(filename, image, palette);
}

int write_image_png(const char *filename, ImageData *image) {
    return writer_save_png(filename, image);
}
