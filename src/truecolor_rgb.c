/* True color RGB image generation
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datanc.h"
#include "image.h"
#include "logger.h"
#include "rayleigh.h"
#include "reader_nc.h"
#include "rgb.h"

ImageData create_truecolor_rgb(DataF c01_blue, DataF c02_red, DataF c03_nir) {
  double start = omp_get_wtime();

  // Crear el canal verde sintético
  DataF green_ch = dataf_create(c02_red.width, c02_red.height);
  if (green_ch.data_in == NULL) {
    LOG_ERROR("Falla de memoria al crear el canal verde sintético.");
    return image_create(0, 0, 0);
  }

  float g_min = 1e20f, g_max = -1e20f;
  #pragma omp parallel for reduction(min:g_min) reduction(max:g_max)
  for (size_t i = 0; i < green_ch.size; i++) {
      float r = c02_red.data_in[i];
      float b = c01_blue.data_in[i];
      float n = c03_nir.data_in[i];
      
      // Verificar píxeles inválidos (NonData es 1e+32)
      if (r > 1e20f || b > 1e20f || n > 1e20f || 
          r < 0.0f || b < 0.0f || n < 0.0f ||
          isnan(r) || isnan(b) || isnan(n) ||
          isinf(r) || isinf(b) || isinf(n)) {
          green_ch.data_in[i] = NonData;
      } else {
          // Fórmula EDC (Earth Data Collaborative): C01=0.45706946, C02=0.48358168, C03=0.06038137
          float g = 0.45706946f * b + 0.48358168f * r + 0.06038137f * n;
          // Verificación de seguridad post-cálculo
          if (g > 100.0f || g < 0.0f || isnan(g) || isinf(g)) {
              green_ch.data_in[i] = NonData;
          } else {
              green_ch.data_in[i] = g;
              if (g < g_min) g_min = g;
              if (g > g_max) g_max = g;
          }
      }
  }
  green_ch.fmin = g_min;
  green_ch.fmax = g_max;

  // Usar la función genérica de creación de RGB, pero pasando los rangos dinámicos
  // reales de cada canal. Esto asegura que la normalización inicial sea correcta
  // y que los píxeles NonData (valor 0) permanezcan negros después de la
  // ecualización del histograma.
  ImageData imout = create_multiband_rgb(&c02_red, &green_ch, &c01_blue, c02_red.fmin, c02_red.fmax, green_ch.fmin, green_ch.fmax, c01_blue.fmin, c01_blue.fmax);
  dataf_destroy(&green_ch);

  double end = omp_get_wtime();
  LOG_INFO("Tiempo RGB %lf\n", end - start);
  return imout;
}

/**
 * @brief Crea imagen true color RGB con corrección atmosférica de Rayleigh opcional.
 * 
 * @param c01_blue Canal 01 (Blue) - reflectancia TOA
 * @param c02_red Canal 02 (Red) - reflectancia TOA
 * @param c03_nir Canal 03 (NIR) - reflectancia TOA
 * @param filename_ref Nombre de archivo NetCDF para extraer geometría (puede ser NULL)
 * @param apply_rayleigh Si true, aplica corrección Rayleigh
 * @return ImageData estructura con la imagen RGB generada
 */
ImageData create_truecolor_rgb_rayleigh(DataF c01_blue, DataF c02_red, DataF c03_nir,
                                        const char *filename_ref, bool apply_rayleigh) {
  double start_total = omp_get_wtime();
  
  // Si no se solicita Rayleigh, usar versión original
  if (!apply_rayleigh || filename_ref == NULL) {
    LOG_INFO("Generando true color sin corrección Rayleigh.");
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }
  
  LOG_INFO("Generando true color CON corrección atmosférica de Rayleigh...");
  LOG_INFO("Dimensiones de entrada:");
  LOG_INFO("  C01 (Blue): %zux%zu = %zu píxeles", c01_blue.width, c01_blue.height, c01_blue.size);
  LOG_INFO("  C02 (Red):  %zux%zu = %zu píxeles", c02_red.width, c02_red.height, c02_red.size);
  LOG_INFO("  C03 (NIR):  %zux%zu = %zu píxeles", c03_nir.width, c03_nir.height, c03_nir.size);
  
  // Verificar que todas las bandas tienen el mismo tamaño (deberían estar ya downsampleadas)
  if (c01_blue.size != c02_red.size || c01_blue.size != c03_nir.size) {
    LOG_ERROR("Las bandas no tienen el mismo tamaño. Debe aplicar downsampling antes de llamar a esta función.");
    LOG_ERROR("Procediendo sin corrección Rayleigh.");
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }
  
  size_t target_width = c01_blue.width;
  size_t target_height = c01_blue.height;
  LOG_INFO("Tamaño objetivo para geometría: %zux%zu", target_width, target_height);
  
  // 1. Calcular navegación (lat/lon) - esto será al tamaño de C01 original
  DataF navla, navlo;
  if (compute_navigation_nc(filename_ref, &navla, &navlo) != 0) {
    LOG_ERROR("Error al calcular navegación. Procediendo sin corrección Rayleigh.");
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }
  
  LOG_INFO("Navegación calculada: %zux%zu píxeles", navla.width, navla.height);
  
  // Si la navegación es más grande que las bandas, hacer downsampling
  if (navla.size != c01_blue.size) {
    LOG_INFO("Haciendo downsampling de navegación para coincidir con bandas...");
    int downsample_factor = (int)(navla.width / target_width);
    if (downsample_factor > 1) {
      LOG_INFO("  Factor de downsampling: %d", downsample_factor);
      DataF navla_down = downsample_boxfilter(navla, downsample_factor);
      DataF navlo_down = downsample_boxfilter(navlo, downsample_factor);
      dataf_destroy(&navla);
      dataf_destroy(&navlo);
      navla = navla_down;
      navlo = navlo_down;
      LOG_INFO("  Navegación downsampleada a: %zux%zu", navla.width, navla.height);
    } else {
      LOG_ERROR("Dimensiones de navegación no coinciden y no se puede corregir.");
      dataf_destroy(&navla);
      dataf_destroy(&navlo);
      return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
    }
  }
  
  // 2. Calcular geometría solar
  DataF sza, saa;
  if (compute_solar_angles_nc(filename_ref, &navla, &navlo, &sza, &saa) != 0) {
    LOG_ERROR("Error al calcular ángulos solares. Procediendo sin corrección Rayleigh.");
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }
  
  // 3. Calcular geometría del satélite
  DataF vza, vaa;
  if (compute_satellite_angles_nc(filename_ref, &navla, &navlo, &vza, &vaa) != 0) {
    LOG_ERROR("Error al calcular ángulos del satélite. Procediendo sin corrección Rayleigh.");
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    dataf_destroy(&sza);
    dataf_destroy(&saa);
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }
  
  // 4. Calcular azimut relativo
  DataF raa;
  compute_relative_azimuth(&saa, &vaa, &raa);
  
  LOG_INFO("Geometría calculada:");
  LOG_INFO("  SZA: %zux%zu = %zu píxeles", sza.width, sza.height, sza.size);
  LOG_INFO("  VZA: %zux%zu = %zu píxeles", vza.width, vza.height, vza.size);
  LOG_INFO("  RAA: %zux%zu = %zu píxeles", raa.width, raa.height, raa.size);
  
  // Ya no necesitamos SAA y VAA
  dataf_destroy(&saa);
  dataf_destroy(&vaa);
  dataf_destroy(&navla);
  dataf_destroy(&navlo);
  
  // 5. Cargar LUTs de Rayleigh (solo para C01 y C02, NO para C03/NIR)
  // Según estándar geo2grid/satpy, C03 (NIR) NO recibe corrección Rayleigh
  // porque Rayleigh afecta principalmente longitudes de onda cortas (azul)
  LOG_INFO("Cargando LUTs de Rayleigh para C01 (Blue) y C02 (Red)...");
  RayleighLUT lut_c01 = rayleigh_lut_load("rayleigh_lut_C01.bin");
  RayleighLUT lut_c02 = rayleigh_lut_load("rayleigh_lut_C02.bin");
  
  if (lut_c01.table == NULL || lut_c02.table == NULL) {
    LOG_ERROR("Error al cargar LUTs. Procediendo sin corrección Rayleigh.");
    rayleigh_lut_destroy(&lut_c01);
    rayleigh_lut_destroy(&lut_c02);
    dataf_destroy(&sza);
    dataf_destroy(&vza);
    dataf_destroy(&raa);
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }
  
  // 6. Crear copias de los canales para aplicar corrección
  DataF c01_corrected = dataf_create(c01_blue.width, c01_blue.height);
  DataF c02_corrected = dataf_create(c02_red.width, c02_red.height);
  
  // Copiar datos originales
  memcpy(c01_corrected.data_in, c01_blue.data_in, c01_blue.size * sizeof(float));
  memcpy(c02_corrected.data_in, c02_red.data_in, c02_red.size * sizeof(float));
  
  // 7. Aplicar corrección Rayleigh SOLO a C01 (Blue) y C02 (Red)
  // C03 (NIR) se usa SIN corrección según estándar geo2grid/satpy
  LOG_INFO("Aplicando corrección Rayleigh a C01 (Blue) y C02 (Red)...");
  LOG_INFO("C03 (NIR) se usará SIN corrección Rayleigh (estándar geo2grid/satpy).");
  apply_rayleigh_correction(&c01_corrected, &sza, &vza, &raa, &lut_c01);
  apply_rayleigh_correction(&c02_corrected, &sza, &vza, &raa, &lut_c02);
  
  // Liberar recursos de geometría y LUTs
  dataf_destroy(&sza);
  dataf_destroy(&vza);
  dataf_destroy(&raa);
  rayleigh_lut_destroy(&lut_c01);
  rayleigh_lut_destroy(&lut_c02);
  
  // 8. Crear canal verde sintético usando C01/C02 corregidos + C03 original
  // Según geo2grid/satpy: green usa C01(rayleigh), C02(rayleigh), C03(sin rayleigh)
  LOG_INFO("Creando canal verde sintético con C01/C02 corregidos + C03 original...");
  DataF green_ch = dataf_create(c01_corrected.width, c01_corrected.height);
  if (green_ch.data_in == NULL) {
    LOG_ERROR("Falla de memoria al crear el canal verde sintético.");
    dataf_destroy(&c01_corrected);
    dataf_destroy(&c02_corrected);
    return create_truecolor_rgb(c01_blue, c02_red, c03_nir);
  }

  float g_min = 1e20f, g_max = -1e20f;
  #pragma omp parallel for reduction(min:g_min) reduction(max:g_max)
  for (size_t i = 0; i < green_ch.size; i++) {
    float b = c01_corrected.data_in[i];  // Blue corregido
    float r = c02_corrected.data_in[i];  // Red corregido
    float n = c03_nir.data_in[i];        // NIR original (sin Rayleigh)
    
    // CRÍTICO: verificar todos los píxeles inválidos
    // C03 puede tener NonData (>1e30) porque no pasó por corrección Rayleigh
    // C01/C02 tienen 0.0f en píxeles nocturnos/inválidos tras Rayleigh
    if (n > 1e20f || n < 0.0f || b <= 0.0f || r <= 0.0f || 
        isnan(n) || isinf(n) || isnan(b) || isnan(r)) {
      green_ch.data_in[i] = NonData;
    } else {
      // Fórmula EDC: C01=0.45706946, C02=0.48358168, C03=0.06038137
      float g = 0.45706946f * b + 0.48358168f * r + 0.06038137f * n;
      // Verificación de seguridad: asegurar que g es razonable
      if (g > 100.0f || g < 0.0f || isnan(g) || isinf(g)) {
        green_ch.data_in[i] = NonData;
      } else {
        green_ch.data_in[i] = g;
        if (g < g_min) g_min = g;
        if (g > g_max) g_max = g;
      }
    }
  }
  green_ch.fmin = g_min;
  green_ch.fmax = g_max;
  
  LOG_INFO("Verde sintético: min=%.4f, max=%.4f", g_min, g_max);
  
  // 9. Generar imagen RGB usando los rangos correctos de cada canal
  LOG_INFO("Generando imagen RGB con canales procesados correctamente...");
  LOG_INFO("  C02 (Red):   [%.6f, %.6f]", c02_corrected.fmin, c02_corrected.fmax);
  LOG_INFO("  Green (syn): [%.6f, %.6f]", green_ch.fmin, green_ch.fmax);
  LOG_INFO("  C01 (Blue):  [%.6f, %.6f]", c01_corrected.fmin, c01_corrected.fmax);
  ImageData imout = create_multiband_rgb(&c02_corrected, &green_ch, &c01_corrected,
                                        c02_corrected.fmin, c02_corrected.fmax,
                                        green_ch.fmin, green_ch.fmax,
                                        c01_corrected.fmin, c01_corrected.fmax);
  
  // Liberar canales
  dataf_destroy(&c01_corrected);
  dataf_destroy(&c02_corrected);
  dataf_destroy(&green_ch);
  
  double end_total = omp_get_wtime();
  LOG_INFO("Tiempo total RGB con Rayleigh: %.3f segundos", end_total - start_total);
  
  return imout;
}
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max) {
    if (!r_ch || !g_ch || !b_ch || !r_ch->data_in || !g_ch->data_in || !b_ch->data_in) {
        LOG_ERROR("Invalid input channels for create_multiband_rgb");
        return image_create(0, 0, 0);
    }

    if (r_ch->width != g_ch->width || r_ch->height != g_ch->height ||
        r_ch->width != b_ch->width || r_ch->height != b_ch->height) {
        LOG_ERROR("Channel dimensions mismatch in create_multiband_rgb");
        return image_create(0, 0, 0);
    }

    ImageData imout = image_create(r_ch->width, r_ch->height, 3);
    if (imout.data == NULL) {
        LOG_ERROR("Memory allocation failed for output image");
        return image_create(0, 0, 0);
    }

    size_t size = r_ch->size;
    float r_range = r_max - r_min;
    float g_range = g_max - g_min;
    float b_range = b_max - b_min;

    // Avoid division by zero
    if (fabs(r_range) < 1e-6) r_range = 1.0f;
    if (fabs(g_range) < 1e-6) g_range = 1.0f;
    if (fabs(b_range) < 1e-6) b_range = 1.0f;

    #pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
        float r_val = r_ch->data_in[i];
        float g_val = g_ch->data_in[i];
        float b_val = b_ch->data_in[i];

        uint8_t r_byte = 0, g_byte = 0, b_byte = 0;

        if (!IS_NONDATA(r_val)) {
            float norm = (r_val - r_min) / r_range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            r_byte = (uint8_t)(norm * 255.0f);
        }

        if (!IS_NONDATA(g_val)) {
            float norm = (g_val - g_min) / g_range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            g_byte = (uint8_t)(norm * 255.0f);
        }

        if (!IS_NONDATA(b_val)) {
            float norm = (b_val - b_min) / b_range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            b_byte = (uint8_t)(norm * 255.0f);
        }

        size_t idx = i * 3;
        imout.data[idx] = r_byte;
        imout.data[idx + 1] = g_byte;
        imout.data[idx + 2] = b_byte;
    }

    return imout;
}
