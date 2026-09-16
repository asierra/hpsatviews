/* Nighttime pseudocolor imagery from ABI C13 brightness temperature.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <math.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>

#include "datanc.h"
#include "image.h"
#include "palette.h"
#include "logger.h"
#include "timing.h"

ImageData create_nocturnal_pseudocolor(const DataF* temp_data, const ImageData* fondo) {
  if (!temp_data || !temp_data->data_in) {
    LOG_ERROR("Invalid temperature data for create_nocturnal_pseudocolor.");
    return image_create(0, 0, 0); // return empty image on invalid input
  }

  ImageData imout = image_create(temp_data->width, temp_data->height, 3);
  
  if (imout.data == NULL) {
    LOG_ERROR("Failed to allocate memory for nocturnal image.");
    return imout;
  }

  double start = omp_get_wtime();

  const float max_ir_temp = 263.15f; // upper bound for high cold clouds (~-10°C)

#pragma omp parallel for
  for (unsigned int y = 0; y < imout.height; y++) {
    for (unsigned int x = 0; x < imout.width; x++) {
      size_t i = (size_t)y * imout.width + x;
      size_t po = i * imout.bpp;
      unsigned char r, g, b;

      r = g = b = 0;
      float f = temp_data->data_in[i];

      if (!IS_NONDATA(f)) {
        unsigned int t;
        for (t = 0; t < 255; t++)
          if (f >= atmosrainbow[t].d && f < atmosrainbow[t + 1].d)
            break;

        // If t == 255, the value is >= the last threshold; clamp to 254
        // to avoid an out-of-bounds access on paleta[t+1].
        if (t == 255) t = 254;

        r = (unsigned char)(255 * atmosrainbow[t].r);
        g = (unsigned char)(255 * atmosrainbow[t].g);
        b = (unsigned char)(255 * atmosrainbow[t].b);

        if (fondo && f > max_ir_temp) {
          float w = 1. - atmosrainbow[t].a;
          size_t pf = i * fondo->bpp;
          r = (unsigned char)(r * (1 - w) + w * fondo->data[pf]);
          g = (unsigned char)(g * (1 - w) + w * fondo->data[pf + 1]);
          b = (unsigned char)(b * (1 - w) + w * fondo->data[pf + 2]);
        }
      }

      // Las escrituras van FUERA del if: los píxeles NonData (fuera del disco)
      // deben quedar en el negro que ya tienen r/g/b. Estando dentro, no se
      // escribían nunca y conservaban la basura de malloc —image_create() no
      // inicializa—, que daynite luego mezclaba en la imagen final. El valor
      // dependía del historial de asignaciones del proceso, así que la salida
      // cambiaba según qué se hubiera reservado y liberado antes.
      imout.data[po] = r;
      imout.data[po + 1] = g;
      imout.data[po + 2] = b;
    }
  }

  double end = omp_get_wtime();
  LOG_TIMING_STAGE(TM_COMPOSE, end - start, "Nocturnal pseudocolor");

  return imout;
}


/* Realce IR diurno (--ir-overlay): superpone la paleta infrarroja sobre el
 * color verdadero ya compuesto, en vez de sustituirlo como hace la máscara
 * día/noche. Así el lado diurno muestra los topes convectivos fríos sin dejar
 * de ser color verdadero.
 *
 * El peso no sale del alfa de la paleta, cuya curva está pensada para la noche
 * —a 295 K todavía vale 0.29 y deslavaría el color verdadero entero—, sino de
 * una rampa explícita:
 *
 *   T <= t_opaque          : infrarrojo puro
 *   t_opaque < T < t_clear : desvanecido lineal
 *   T >= t_clear           : base intacta
 *
 * Los umbrales por omisión (220-240 K) salen de barrer escenas GOES-19 de disco
 * completo: con t_clear de 250 K en adelante se tiñen el hielo antártico y la
 * nube media diurna, y el estratocúmulo marino no se tiñe en ningún caso. */
void image_overlay_ir(ImageData *base, const DataF *temp, const ImageData *night_mask,
                      float t_opaque, float t_clear) {
  if (!base || !base->data || !temp || !temp->data_in) return;
  if (base->bpp < 3) return;
  if ((size_t)base->width * base->height != temp->size) {
    LOG_WARN("IR overlay: image %ux%u does not match C13 (%zu px); skipped",
             base->width, base->height, temp->size);
    return;
  }
  if (night_mask && (!night_mask->data || night_mask->width != base->width ||
                     night_mask->height != base->height)) {
    night_mask = NULL;
  }
  float span = t_clear - t_opaque;
  if (span <= 0.0f) return;

  double start = omp_get_wtime();
  size_t n = temp->size;
  unsigned int bpp = base->bpp;
  const unsigned char *mask = night_mask ? night_mask->data : NULL;

#pragma omp parallel for
  for (size_t i = 0; i < n; i++) {
    float f = temp->data_in[i];
    if (IS_NONDATA(f) || f >= t_clear) continue;
    if (mask && mask[i] == 255) continue; // la mezcla lo sustituye entero

    float a = (t_clear - f) / span;
    if (a > 1.0f) a = 1.0f;

    unsigned int t = atmosrainbow_index(f);
    size_t po = i * bpp;
    float pr = 255.0f * atmosrainbow[t].r;
    float pg = 255.0f * atmosrainbow[t].g;
    float pb = 255.0f * atmosrainbow[t].b;
    base->data[po]     = (unsigned char)(pr * a + base->data[po] * (1.0f - a));
    base->data[po + 1] = (unsigned char)(pg * a + base->data[po + 1] * (1.0f - a));
    base->data[po + 2] = (unsigned char)(pb * a + base->data[po + 2] * (1.0f - a));
  }

  LOG_TIMING_STAGE(TM_COMPOSE, omp_get_wtime() - start,
                   "IR overlay on day side (%.1f-%.1f K)", t_opaque, t_clear);
}
