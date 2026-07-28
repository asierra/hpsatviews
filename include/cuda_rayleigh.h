/* Device-resident Rayleigh atmospheric correction (LUT) + solar-zenith
 * correction, operating on DataFDev buffers so the true-color Rayleigh chain
 * stays on the GPU and reuses the channels already uploaded.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Solo bajo #ifdef HPSV_CUDA; implementación en src/cuda/rayleigh_cuda.cu.
 */
#ifndef HPSATVIEWS_CUDA_RAYLEIGH_H_
#define HPSATVIEWS_CUDA_RAYLEIGH_H_

#include "cuda_dataf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LUT de Rayleigh con la tabla en memoria de device. Metadatos idénticos a
 * RayleighLUT (rayleigh.h); la tabla (~65 KB, excede __constant__ de 64 KB) va
 * a memoria global y se lee con acceso trilineal por hilo. */
typedef struct {
  float *d_table;                ///< tabla plana [SZA][VZA][AZ] en device
  uint8_t channel;               ///< canal ABI de la LUT (para el log [PERF])
  int n_sz, n_vz, n_az;
  float sz_min, sz_max, sz_step;
  float vz_min, vz_max, vz_step;
  float az_min, az_max, az_step;
} RayleighLUTDev;

/* Carga la LUT embebida del canal (reusa rayleigh_lut_load_from_memory del host)
 * y sube su tabla a device. d_table == NULL si falla. */
RayleighLUTDev rayleigh_lut_dev_load(uint8_t channel);

/* Libera la tabla de device. */
void rayleigh_lut_dev_destroy(RayleighLUTDev *lut);

/* Corrección por ángulo cenital solar, in place sobre un DataFDev. Réplica de
 * apply_solar_zenith_correction() (src/truecolor.c) sin la reducción min/max
 * (no se usa en truecolor). */
void apply_solar_zenith_correction_dev(DataFDev *data, const DataFDev *sza);

/* Corrección Rayleigh LUT, in place sobre img (device). Réplica de
 * luts_rayleigh_correction() (src/rayleigh.c): night mask, clamp de ángulos,
 * lookup trilineal, taper 70–88°, relajación por nubes con redband (>=0.20).
 * redband puede ser NULL (canal sin relajación). No recalcula fmin/fmax (fijo
 * en truecolor). */
void luts_rayleigh_correction_dev(DataFDev *img, const DataFDev *sza,
                                  const DataFDev *vza, const DataFDev *raa,
                                  const RayleighLUTDev *lut,
                                  const DataFDev *redband);

#ifdef __cplusplus
}
#endif

#endif /* HPSATVIEWS_CUDA_RAYLEIGH_H_ */
