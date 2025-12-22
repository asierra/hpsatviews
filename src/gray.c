/* Creates a single BW image from an original NC.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "image.h"
#include "logger.h"
#include "reader_cpt.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha, const CPTData* cpt) {
  unsigned int bpp = use_alpha ? 2 : 1;

  ImageData imout = image_create(c01.width, c01.height, bpp);
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen en gray.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando loop gray iw %lu ih %lu min %g max %g", imout.width, imout.height, c01.fmin, c01.fmax);

  uint8_t last_color = (cpt && cpt->has_nan_color) ? cpt->num_colors-1: 255;
  #pragma omp parallel for
  for (unsigned int y = 0; y < imout.height; y++) {
    for (unsigned int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      uint8_t r = 0, a = 0;

      // Since DataF is now always float, we only need the float processing logic.
      if (c01.data_in[i] != NonData) {
        float range = c01.fmax - c01.fmin;
        float normalized_val;
        if (invert_value)
          normalized_val = (c01.fmax - c01.data_in[i]) / range;
        else
          normalized_val = (c01.data_in[i] - c01.fmin) / range;
        r = (unsigned char)(last_color * normalized_val);
        a = 255;
      } else {
        // NonData pixel
        if (use_alpha) {
          // Con -a, NonData es transparente
          r = 0;
          a = 0;
        } else if (cpt && cpt->has_nan_color) {
          // Sin -a pero con nan_color, mostrar color NaN
          r = last_color;
          a = 255;
        } else {
          // Sin -a y sin nan_color
          r = 0;
          a = 0;
        }
      }

      // This block was unused and caused a warning. It is now removed.
      if (cpt != NULL && 0) { // The '&& 0' makes this block dead code.
        // Color c = get_color_for_value(cpt, (double)r);
        // r = (unsigned char)(((double)r / 255.0) * (cpt->entry_count > 0 ? cpt->entry_count - 1 : 255));
      }

      imout.data[po] = r;
      if (imout.bpp == 2) {
        imout.data[po + 1] = a;
      }
    }
  }
  double end = omp_get_wtime();
  LOG_INFO("Tiempo Single Gray %lf", end - start);
  return imout;
}

/**
 * @brief Crea imagen en escala de grises usando un rango personalizado.
 * @param c01 Datos flotantes de entrada.
 * @param invert_value Invertir valores (blanco<->negro).
 * @param use_alpha Añadir canal alfa para NonData.
 * @param min_val Valor mínimo del rango para normalización.
 * @param max_val Valor máximo del rango para normalización.
 * @return Imagen creada (debe liberarse con image_destroy).
 */
ImageData create_single_gray_range(DataF c01, bool invert_value, bool use_alpha,
                                   float min_val, float max_val) {
  unsigned int bpp = use_alpha ? 2 : 1;

  ImageData imout = image_create(c01.width, c01.height, bpp);
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen en gray.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando loop gray con rango [%.2f, %.2f] iw %lu ih %lu", 
           min_val, max_val, imout.width, imout.height);

  float range = max_val - min_val;
  if (range == 0.0f) range = 1.0f; // Evitar división por cero

  #pragma omp parallel for
  for (unsigned int y = 0; y < imout.height; y++) {
    for (unsigned int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      uint8_t r = 0, a = 0;

      if (c01.data_in[i] != NonData && !IS_NONDATA(c01.data_in[i])) {
        float val = c01.data_in[i];
        
        // Clamp al rango especificado
        if (val < min_val) val = min_val;
        if (val > max_val) val = max_val;
        
        float normalized_val;
        if (invert_value)
          normalized_val = (max_val - val) / range;
        else
          normalized_val = (val - min_val) / range;
        
        r = (unsigned char)(255.0f * normalized_val);
        a = 255;
      } else {
        // NonData pixel
        if (use_alpha) {
          r = 0;
          a = 0;
        } else {
          r = 0;
          a = 0;
        }
      }

      imout.data[po] = r;
      if (imout.bpp == 2) {
        imout.data[po + 1] = a;
      }
    }
  }

  double end = omp_get_wtime();
  LOG_INFO("Tiempo Single Gray con rango personalizado: %lf", end - start);
  return imout;
}

ImageData create_single_grayb(DataB c01, bool invert_value, bool use_alpha, const CPTData* cpt) {
  unsigned int bpp = use_alpha ? 2 : 1;

  ImageData imout = image_create(c01.width, c01.height, bpp);
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen en grayb.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando loop grayb iw %lu ih %lu min %d max %d", imout.width, imout.height, c01.min, c01.max);

  uint8_t last_color = (cpt && cpt->has_nan_color) ? cpt->num_colors-1: 0;
  #pragma omp parallel for
  for (unsigned int y = 0; y < imout.height; y++) {
    for (unsigned int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      uint8_t r = 0, a = 0;

      // Para DataB, el valor NonData es -128
      if (c01.data_in[i] != -128) {
        uint8_t val = (uint8_t)c01.data_in[i];
        if (invert_value) {
            r = 255 - val; // Asumiendo que el rango de valores es 0-255
        } else {
            r = val;
        }
        a = 255;
      } else {
        // NonData pixel
        if (use_alpha) {
          // Con -a, NonData es transparente
          r = 0;
          a = 0;
        } else if (cpt && cpt->has_nan_color) {
          // Sin -a pero con nan_color, mostrar color NaN
          r = last_color;
          a = 255;
        } else {
          // Sin -a y sin nan_color
          r = 0;
          a = 0;
        }
      }

      imout.data[po] = r;
      if (imout.bpp == 2) {
        imout.data[po + 1] = a;
      }
    }
  }
  double end = omp_get_wtime();
  LOG_INFO("Tiempo Single Gray (byte) %lf", end - start);
  return imout;
}
