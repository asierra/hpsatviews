/* Rayleigh atmospheric correction for GOES ABI visible bands
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_RAYLEIGH_H_
#define HPSATVIEWS_RAYLEIGH_H_

#include "datanc.h"
#include <stdbool.h>

typedef struct {
    DataF sza; // Solar Zenith Angle
    DataF vza; // View Zenith Angle
    DataF raa; // Relative Azimuth Angle
} RayleighNav;


/**
 * @brief Aplica corrección Rayleigh analítica (fórmula física).
 * No requiere LUTs externas.
 * * @param img Imagen a corregir (modificada in-situ)
 * @param nav Estructura con la navegación (SZA, VZA, RAA)
 * @param tau Coeficiente de profundidad óptica de la banda (ej. 0.061 para azul)
 */
//void analytic_rayleigh_correction(DataF *img, const RayleighNav *nav, float tau);
void analytic_rayleigh_correction(DataF *band, const RayleighNav *nav, float lambda_um);

/**
 * @brief Carga y calcula toda la geometría necesaria para la corrección Rayleigh.
 * Internamente gestiona la creación y liberación de navla, navlo, saa y vaa.
 * * @param filename Ruta al archivo NetCDF L1b.
 * @param nav Puntero a la estructura RayleighNav a rellenar.
 * @return true si tuvo éxito, false si falló.
 */
bool rayleigh_load_navigation(const char *filename, RayleighNav *nav, 
				unsigned int target_width, unsigned int target_height);

/**
 * @brief Igual que rayleigh_load_navigation pero reutiliza lat/lon ya calculados.
 * Evita llamar a compute_navigation_nc cuando el contexto ya dispone de la
 * navegación geográfica.
 */
bool rayleigh_load_navigation_from_latlon(const char *filename,
                                          const DataF *navla, const DataF *navlo,
                                          RayleighNav *nav,
                                          unsigned int target_width,
                                          unsigned int target_height);


// Rayleigh Lookup Table structure (simple version with min/max/step)
typedef struct {
    float *table;       // Flat array with precalculated values
    int n_sz, n_vz, n_az; // Dimensions (Solar Zen, View Zen, Azimuth)
    float sz_min, sz_max, sz_step; // Solar Zenith range
    float vz_min, vz_max, vz_step; // View Zenith range
    float az_min, az_max, az_step; // Relative Azimuth range
} RayleighLUT;

/**
 * @brief Libera la memoria de la estructura de navegación.
 */
void rayleigh_free_navigation(RayleighNav *nav);

/**
 * @brief Apply Rayleigh atmospheric correction to a reflectance image.
 * 
 * Modifies the input image in-place, subtracting Rayleigh scattering
 * contribution based on viewing geometry.
 * 
 * @param img Input/output reflectance image (modified in-place)
 * @param nav Navigation data (SZA, VZA, RAA)
 * @param name Channel name for LUT selection ("C01", "C02", "C03")
 */
void luts_rayleigh_correction(DataF *img, const RayleighNav *nav, const uint8_t channel, const DataF *redband);

/**
 * @brief Free memory allocated for a Rayleigh LUT.
 * 
 * @param lut Pointer to the LUT structure to free
 */
void rayleigh_lut_destroy(RayleighLUT *lut);

#endif /* HPSATVIEWS_RAYLEIGH_H_ */
