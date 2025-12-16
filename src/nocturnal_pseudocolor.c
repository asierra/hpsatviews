/*
 * Generación de imagen de pseudocolor nocturno.
 * 
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>

#include "datanc.h"
#include "image.h"
#include "paleta.h"
#include "logger.h"

/**
 * @brief Genera una imagen de pseudocolor nocturno a partir de datos de temperatura de brillo.
 *
 * Esta función toma datos de temperatura de brillo en Kelvin (banda 13 de GOES),
 * los mapea a una paleta de colores (definida en paleta.h) y opcionalmente los mezcla
 * con una imagen de fondo (como luces de ciudad) para crear una visualización nocturna.
 *
 * @param temp_data Puntero a la estructura DataF con los datos de temperatura.
 * @param fondo Puntero opcional a una imagen de fondo (ImageData). Si es NULL, no se usa fondo.
 * @return Una estructura ImageData con la imagen RGB resultante. Si falla, la imagen
 *         devuelta tendrá ancho y alto cero. El llamador es responsable de liberar
 *         la memoria de la imagen con image_destroy().
 */
ImageData create_nocturnal_pseudocolor(const DataF* temp_data, const ImageData* fondo) {
  if (!temp_data || !temp_data->data_in) {
    LOG_ERROR("Datos de temperatura inválidos para create_nocturnal_pseudocolor.");
    return image_create(0, 0, 0); // Devuelve imagen vacía
  }

  ImageData imout = image_create(temp_data->width, temp_data->height, 3);
  
  if (imout.data == NULL) {
    LOG_ERROR("No fue posible apartar memoria para la imagen nocturna.");
    return imout;
  }

  double start = omp_get_wtime();
  LOG_INFO("Iniciando generación de pseudocolor nocturno...");

  const float max_ir_temp = 263.15f; // Límite de temperatura para nubes altas (aprox -10°C)

#pragma omp parallel for
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      size_t i = (size_t)y * imout.width + x;
      size_t po = i * imout.bpp;
      unsigned char r, g, b;

      r = g = b = 0;
      float f = temp_data->data_in[i];

      if (!IS_NONDATA(f)) {
        int t;
        for (t = 0; t < 255; t++)
          if (f >= paleta[t].d && f < paleta[t + 1].d)
            break;

        // Si el bucle termina, 't' será 255, lo que significa que el valor
        // es mayor o igual al último umbral. Clampeamos 't' a 254 para
        // evitar un acceso fuera de límites en paleta[t+1].
        if (t == 255) t = 254;

        r = (unsigned char)(255 * paleta[t].r);
        g = (unsigned char)(255 * paleta[t].g);
        b = (unsigned char)(255 * paleta[t].b);

        if (fondo && f > max_ir_temp) {
          float w = 1. - paleta[t].a;
          size_t pf = i * fondo->bpp;
          r = (unsigned char)(r * (1 - w) + w * fondo->data[pf]);
          g = (unsigned char)(g * (1 - w) + w * fondo->data[pf + 1]);
          b = (unsigned char)(b * (1 - w) + w * fondo->data[pf + 2]);
        }

        imout.data[po] = r;
        imout.data[po + 1] = g;
        imout.data[po + 2] = b;
      }
    }
  }

  double end = omp_get_wtime();
  LOG_INFO("Pseudocolor nocturno generado en %.3f segundos.", end - start);

  return imout;
}
