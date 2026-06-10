/* Rayleigh atmospheric correction for GOES-R ABI visible bands.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include "datanc.h"
#include "logger.h"
#include "rayleigh.h"
#include "rayleigh_lut_embedded.h"
#include "reader_nc.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// Funciones comunes

static void enforce_resolution(DataF *data, unsigned int target_w, unsigned int target_h) {
    if (data == NULL || data->data_in == NULL) return;

    if (data->width == target_w && data->height == target_h) return;

    // Navigation grid is larger than target: downsample.
    if (data->width > target_w) {
        int factor = data->width / target_w;
        if (factor < 1) factor = 1;

        LOG_DEBUG("Ajustando navegación Rayleigh: %dx%d -> %dx%d (downsample factor %d)", 
                 data->width, data->height, target_w, target_h, factor);
        
        DataF resized = downsample_boxfilter(*data, factor);
        dataf_destroy(data);
        *data = resized;
    }
    // Navigation grid is smaller than target: upsample (needed for --full-res).
    else if (data->width < target_w) {
        int factor = target_w / data->width;
        if (factor < 1) factor = 1;

        LOG_DEBUG("Ajustando navegación Rayleigh: %dx%d -> %dx%d (upsample factor %d)", 
                 data->width, data->height, target_w, target_h, factor);
        
        DataF resized = upsample_bilinear(*data, factor);
        dataf_destroy(data);
        *data = resized;
    }
}

void rayleigh_free_navigation(RayleighNav *nav) {
    if (nav) {
        dataf_destroy(&nav->sza);
        dataf_destroy(&nav->vza);
        dataf_destroy(&nav->raa);
    }
}

bool rayleigh_load_navigation_from_latlon(const char *filename,
                                          const DataF *navla, const DataF *navlo,
                                          RayleighNav *nav,
                                          unsigned int target_width, unsigned int target_height) {
    nav->sza.data_in = NULL;
    nav->vza.data_in = NULL;
    nav->raa.data_in = NULL;

    LOG_DEBUG("Generando navegación Rayleigh (SZA, VZA, RAA) desde lat/lon precalculados...");

    // 1. Usar lat/lon ya calculados (copia no-owning mediante cast)
    DataF la = *navla, lo = *navlo;  // shallow copy; data not freed here

    // Compute solar angles (SZA, SAA).
    DataF saa = {0};
    if (compute_solar_angles_nc(filename, &la, &lo, &nav->sza, &saa) != 0) {
        LOG_ERROR("Falla al computar ángulos solares.");
        return false;
    }

    // Compute satellite viewing angles (VZA, VAA).
    DataF vaa = {0};
    if (compute_satellite_angles_nc(filename, &la, &lo, &nav->vza, &vaa) != 0) {
        LOG_ERROR("Falla al computar ángulos del satélite.");
        dataf_destroy(&saa); dataf_destroy(&nav->sza);
        return false;
    }

    // 4. Calcular Azimut Relativo (RAA)
    compute_relative_azimuth(&saa, &vaa, &nav->raa);
    dataf_destroy(&saa);
    dataf_destroy(&vaa);

    if (!nav->sza.data_in || !nav->vza.data_in || !nav->raa.data_in) {
        rayleigh_free_navigation(nav);
        return false;
    }

    if (target_width > 0 && target_height > 0) {
        enforce_resolution(&nav->sza, target_width, target_height);
        enforce_resolution(&nav->vza, target_width, target_height);
        enforce_resolution(&nav->raa, target_width, target_height);
        if (nav->sza.width != target_width) {
            LOG_ERROR("Falla redimensionando navegación Rayleigh: %dx%d != %dx%d",
                      nav->sza.width, nav->sza.height, target_width, target_height);
            rayleigh_free_navigation(nav);
            return false;
        }
    }
    return true;
}

bool rayleigh_load_navigation(const char *filename, RayleighNav *nav, 
				unsigned int target_width, unsigned int target_height) {
    // Inicializar estructura
    nav->sza.data_in = NULL;
    nav->vza.data_in = NULL;
    nav->raa.data_in = NULL;

    LOG_DEBUG("Generando navegación Rayleigh (SZA, VZA, RAA)...");

    // Compute lat/lon navigation needed for angle calculations.
    DataF navla = {0}, navlo = {0};
    if (compute_navigation_nc(filename, &navla, &navlo) != 0) {
        LOG_ERROR("Failed to compute lat/lon navigation.");
        return false;
    }

    // Delegar al helper con lat/lon propios — liberar antes de retornar
    bool ok = rayleigh_load_navigation_from_latlon(filename, &navla, &navlo, nav,
                                                   target_width, target_height);
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    return ok;
}

// =============================================================================
// SECTION 1: RAYLEIGH SCATTERING PHYSICS (Bucholtz 1995)
// =============================================================================

/**
 * Computes Rayleigh optical depth (Tau) using Bucholtz (1995) approximation.
 * @param lambda_um  Central wavelength in micrometres (e.g., 0.47 for C01 blue).
 * @return Tau_R at standard surface pressure (1013.25 mb).
 */
static double calc_bucholtz_tau(double lambda_um) {
    if (lambda_um <= 0) return 0.0;
    
    double l2 = lambda_um * lambda_um;
    double l4 = l2 * l2;
    
    // High-accuracy Bucholtz (1995) formula: Tau = A*lambda^-4*(1 + B/lambda^2 + C/lambda^4)
    // Coefficients for standard atmosphere (P0 = 1013.25 mb).
    return 0.008569 / l4 * (1.0 + 0.0113 / l2 + 0.00013 / l4);
}

/**
 * Rayleigh phase function with depolarization correction (Chandrasekhar).
 * @param cos_theta  Cosine of the scattering angle.
 * @return Phase function value P(Theta).
 */
static float calc_bucholtz_phase(float cos_theta) {
    // Depolarization factor rho_n for standard air (Chandrasekhar formulation).
    const float rho_n = 0.0279f; 
    const float gamma = rho_n / (2.0f - rho_n);
    const float A = 0.75f / (1.0f + 2.0f * gamma);
    const float B = 1.0f + 3.0f * gamma;
    const float C = 1.0f - gamma;
    // P(Theta) = A * [B + C * cos^2(Theta)]
    return A * (B + C * (cos_theta * cos_theta));
}

// =============================================================================
// SECTION 2: ANALYTICAL RAYLEIGH CORRECTION
// =============================================================================

void analytic_rayleigh_correction(DataF *band, const RayleighNav *nav, float lambda_um) {
    if (!band || !nav) {
        LOG_ERROR("Null arguments in analytic_rayleigh_correction");
        return;
    }

    const DataF *sza = &nav->sza;
    const DataF *vza = &nav->vza;
    const DataF *raa = &nav->raa;

    if (!sza->data_in || !vza->data_in || !raa->data_in) {
        LOG_ERROR("Incomplete navigation data in RayleighNav");
        return;
    }

    if (sza->size != band->size)
        LOG_WARN("Navigation size (%zu) does not match band size (%zu).", sza->size, band->size);

    size_t n = band->size;
    
    // Compute Rayleigh optical depth for this wavelength.
    float tau_r = (float)calc_bucholtz_tau(lambda_um);
    
    LOG_DEBUG("Rayleigh analítico: Lambda=%.3f um, Tau=%.4f", lambda_um, tau_r);

    double start_time = omp_get_wtime();
    
    size_t night_pixels = 0;
    size_t valid_pixels = 0;
    size_t clamped_pixels = 0;
    double sum_orig = 0, sum_corr = 0;

    #pragma omp parallel for reduction(+:night_pixels, valid_pixels, clamped_pixels, sum_orig, sum_corr)
    for (size_t i = 0; i < n; i++) {
        float val = band->data_in[i];

        if (IS_NONDATA(val)) {
            band->data_in[i] = NonData;
            continue;
        }

        float sza_val = sza->data_in[i];
        float vza_val = vza->data_in[i];
        
        // Convert geometry to radians.
        float theta_s = sza_val * (float)(M_PI / 180.0);
        float theta_v = vza_val * (float)(M_PI / 180.0);
        float phi_rel = raa->data_in[i] * (float)(M_PI / 180.0);

        float mu_s = cosf(theta_s);
        float mu_v = cosf(theta_v);

        if (mu_s < 0.01f || mu_v < 0.01f) {
            band->data_in[i] = val;
            continue;
        }

        // Scattering angle cosine, Rayleigh phase function, and path-corrected reflectance.
        float cos_scat = -mu_s * mu_v + sinf(theta_s) * sinf(theta_v) * cosf(phi_rel);
        float P_ray = calc_bucholtz_phase(cos_scat);
        float rho_ray = (tau_r * P_ray) / (4.0f * mu_s * mu_v);
        float corrected = val - rho_ray;

        sum_orig += val;
        valid_pixels++;

        if (corrected < 0.0f) {
            corrected = 0.0001f;
            clamped_pixels++;
        }
        sum_corr += corrected;

        band->data_in[i] = corrected;
    }

    double end_time = omp_get_wtime();
    LOG_TIMING(end_time - start_time, "Rayleigh analítico (λ=%.3fμm, %zu px)", lambda_um, valid_pixels);
    
    if (valid_pixels > 0) {
        LOG_DEBUG("  media %.4f -> %.4f, clamped %.1f%%",
            sum_orig/valid_pixels, sum_corr/valid_pixels, 
            100.0 * (double)clamped_pixels / valid_pixels);
    }
}


/*********  LUT-based Rayleigh correction  **********/

/**
 * Trilinear interpolation into a Rayleigh LUT.
 * Inputs are solar zenith secant (s), view zenith secant (v), and relative azimuth in degrees (a).
 */
static inline float get_rayleigh_value(const RayleighLUT *lut, float s, float v, float a) {
    // Clamp all three axes to the LUT range.
    if (s < lut->sz_min) s = lut->sz_min;
    if (s >= lut->sz_max) s = lut->sz_max;
    if (v < lut->vz_min) v = lut->vz_min;
    if (v >= lut->vz_max) v = lut->vz_max;

    // Azimuth symmetry; pyspectral convention: index with (180 - azidiff).
    a = fabsf(a);
    if (a > 180.0f) a = 360.0f - a;
    a = 180.0f - a;
    if (a > lut->az_max) a = lut->az_max;
    if (a < lut->az_min) a = lut->az_min;

    // Floating-point indices into the LUT (using secants directly).
    float idx_s = (s - lut->sz_min) / lut->sz_step;
    float idx_v = (v - lut->vz_min) / lut->vz_step;
    float idx_a = (a - lut->az_min) / lut->az_step;

    // Integer lower-bound indices.
    int s0 = (int)idx_s;
    int v0 = (int)idx_v;
    int a0 = (int)idx_a;

    // Upper-bound indices, clamped to array bounds.
    int s1 = s0 + 1; if (s1 >= lut->n_sz) s1 = lut->n_sz - 1;
    int v1 = v0 + 1; if (v1 >= lut->n_vz) v1 = lut->n_vz - 1;
    int a1 = a0 + 1; if (a1 >= lut->n_az) a1 = lut->n_az - 1;

    // Fractional weights.
    float ds = idx_s - s0;
    float dv = idx_v - v0;
    float da = idx_a - a0;

    // Strides for the flat [SolarZenith][ViewZenith][Azimuth] layout.
    int stride_v = lut->n_az;
    int stride_s = lut->n_vz * lut->n_az;

    float *t = lut->table;

    // 6. Obtener los 8 vecinos (Cubo)
    float c000 = t[s0 * stride_s + v0 * stride_v + a0];
    float c001 = t[s0 * stride_s + v0 * stride_v + a1];
    float c010 = t[s0 * stride_s + v1 * stride_v + a0];
    float c011 = t[s0 * stride_s + v1 * stride_v + a1];
    float c100 = t[s1 * stride_s + v0 * stride_v + a0];
    float c101 = t[s1 * stride_s + v0 * stride_v + a1];
    float c110 = t[s1 * stride_s + v1 * stride_v + a0];
    float c111 = t[s1 * stride_s + v1 * stride_v + a1];

    // 8-corner trilinear interpolation.
    float c00 = c000 * (1.0f - da) + c001 * da;
    float c01 = c010 * (1.0f - da) + c011 * da;
    float c10 = c100 * (1.0f - da) + c101 * da;
    float c11 = c110 * (1.0f - da) + c111 * da;
    float c0  = c00  * (1.0f - dv) + c01  * dv;
    float c1  = c10  * (1.0f - dv) + c11  * dv;
    float result = c0 * (1.0f - ds) + c1 * ds;

    // LUT values are direct reflectance percentages from pyspectral (no /100 needed).
    return result;
}


/**
 * Carga una LUT de Rayleigh desde datos embebidos en memoria.
 * 
 * Formato del archivo:
 * - Header (48 bytes): 9 floats (min, max, step) + 3 ints (dimensiones)
 * - Data: Array 3D float32 [sza][vza][azimuth]
 * 
 * @param data Puntero a los datos binarios embebidos
 * @param data_len Longitud de los datos en bytes
 * @param name Nombre descriptivo para logs (ej: "C01")
 * @return Estructura RayleighLUT cargada (table será NULL si falla)
 */
static RayleighLUT rayleigh_lut_load_from_memory(const uint8_t channel) {
    RayleighLUT lut = {0};
    const unsigned char *data;
    unsigned int data_len;
    // Select embedded LUT for the requested ABI channel.
    if (channel == 1) {
        data = rayleigh_lut_c01_data;
        data_len = rayleigh_lut_c01_data_len;
    } else if (channel == 2) {
        data = rayleigh_lut_c02_data;
        data_len = rayleigh_lut_c02_data_len;
    } else if (channel == 3) {
        data = rayleigh_lut_c03_data;
        data_len = rayleigh_lut_c03_data_len;
    } else {
        LOG_ERROR("Canal de LUT no reconocido %d", channel);
        return lut;
    }
    
    if (!data || data_len < 48) {
        LOG_ERROR("Datos embebidos inválidos para LUT %d", channel);
        return lut;
    }
    
    // Copiar datos a un buffer para leer el header
    const unsigned char *ptr = data;
    
    // Leer header (48 bytes: 9 floats + 3 ints)
    memcpy(&lut.sz_min, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.sz_max, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.sz_step, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.vz_min, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.vz_max, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.vz_step, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.az_min, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.az_max, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.az_step, ptr, sizeof(float)); ptr += sizeof(float);
    memcpy(&lut.n_sz, ptr, sizeof(int)); ptr += sizeof(int);
    memcpy(&lut.n_vz, ptr, sizeof(int)); ptr += sizeof(int);
    memcpy(&lut.n_az, ptr, sizeof(int)); ptr += sizeof(int);
    
    // Validar dimensiones
    if (lut.n_sz <= 0 || lut.n_vz <= 0 || lut.n_az <= 0 ||
        lut.n_sz > 1000 || lut.n_vz > 1000 || lut.n_az > 1000) {
        LOG_ERROR("Dimensiones inválidas en LUT %d: %dx%dx%d", channel, lut.n_sz, lut.n_vz, lut.n_az);
        lut.n_sz = lut.n_vz = lut.n_az = 0;
        return lut;
    }
    
    // Alocar memoria para la tabla
    size_t table_size = (size_t)lut.n_sz * lut.n_vz * lut.n_az;
    size_t expected_size = 48 + table_size * sizeof(float);
    
    if (data_len != expected_size) {
        LOG_ERROR("Tamaño de datos embebidos incorrecto para LUT %d: esperado %zu, obtenido %u", 
                  channel, expected_size, data_len);
        lut.n_sz = lut.n_vz = lut.n_az = 0;
        return lut;
    }
    
    lut.table = malloc(table_size * sizeof(float));
    if (!lut.table) {
        LOG_ERROR("Falla de memoria al alocar LUT %d (%zu valores)", channel, table_size);
        lut.n_sz = lut.n_vz = lut.n_az = 0;
        return lut;
    }
    
    // Copiar datos de la tabla
    memcpy(lut.table, ptr, table_size * sizeof(float));
    
    // Verify LUT value range.
    float min_val = lut.table[0];
    float max_val = lut.table[0];
    for (size_t i = 1; i < table_size; i++) {
        float v = lut.table[i];
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }
    
    LOG_DEBUG("LUT C%02d: %d×%d×%d, rango [%.4f, %.4f]",
             channel, lut.n_sz, lut.n_vz, lut.n_az, min_val, max_val);
    
    return lut;
}


void rayleigh_lut_destroy(RayleighLUT *lut) {
    if (lut && lut->table) {
        free(lut->table);
        lut->table = NULL;
        lut->n_sz = lut->n_vz = lut->n_az = 0;
        LOG_DEBUG("LUT de Rayleigh liberada.");
    }
}


void luts_rayleigh_correction(DataF *img, const RayleighNav *nav, const uint8_t channel, const DataF *redband) {
	// Validar dimensiones
    if (img->width != nav->sza.width || img->height != nav->sza.height) {
        LOG_ERROR("Mismatch dimensiones en Rayleigh Analytic: Img %dx%d vs Nav %dx%d",
                  img->width, img->height, nav->sza.width, nav->sza.height);
        return;
    }
    if (redband && redband->data_in && redband->size != img->size) {
        LOG_WARN("Redband size mismatch (%zu vs %zu), desactivando relajación por nubes",
                 redband->size, img->size);
        redband = NULL;
    }
    RayleighLUT lut = rayleigh_lut_load_from_memory(channel);

    size_t n = img->size;
    double start_time = omp_get_wtime();
    
    // Diagnostic statistics.
    size_t night_pixels = 0;
    size_t negative_pixels = 0;
    size_t valid_pixels = 0;
    double sum_original = 0.0;
    double sum_rayleigh = 0.0;
    double sum_corrected = 0.0;
    float max_rayleigh = 0.0f;
    float min_original = 1e9;
    float max_original = -1e9;

    // OpenMP parallelization; static schedule is optimal since per-pixel cost is uniform.
    #pragma omp parallel for schedule(static) reduction(+:night_pixels,negative_pixels,valid_pixels,sum_original,sum_rayleigh,sum_corrected) reduction(max:max_rayleigh,max_original) reduction(min:min_original)
    for (size_t i = 0; i < n; i++) {
        float theta_s = nav->sza.data_in[i];
        float original = img->data_in[i];
        
        // Skip invalid data - usar macro IS_NONDATA
        if (IS_NONDATA(original)) {
            // Mantener como NonData
            continue;
        }
        
        if (original < min_original) min_original = original;
        if (original > max_original) max_original = original;

        // Skip nighttime/twilight pixels (SZA > 88°); mask to 0.
        // 88° instead of 85° to allow correction in twilight zone.
        if (theta_s > 88.0f || IS_NONDATA(theta_s) || theta_s < 0.0f) {
            img->data_in[i] = 0.0f;
            night_pixels++;
            continue;
        }

        // Clamp angles to LUT valid range (matching pyspectral convention).
        // SZA max = arccos(1/24.75) = 87.68°;  VZA max = arccos(1/3.0) = 70.53°
        float sza_clipped = theta_s;
        if (sza_clipped > 87.68f) sza_clipped = 87.68f;
        if (sza_clipped < 0.0f) sza_clipped = 0.0f;
        
        float vza_clipped = nav->vza.data_in[i];
        if (vza_clipped > 70.53f) vza_clipped = 70.53f;
        if (vza_clipped < 0.0f) vza_clipped = 0.0f;
        
        float theta_s_sec = 1.0f / cosf(sza_clipped * M_PI / 180.0f);
        float vza_sec = 1.0f / cosf(vza_clipped * M_PI / 180.0f);
        
        float r_corr = get_rayleigh_value(&lut, theta_s_sec, vza_sec, nav->raa.data_in[i]);
        
        // Taper correction linearly for SZA 70°-88° to avoid over-correction
        // near the day/night terminator (matching satpy/pyspectral behavior).
        if (theta_s > 70.0f) {
            float reduce_factor = 1.0f - (theta_s - 70.0f) / (88.0f - 70.0f);
            if (reduce_factor < 0.0f) reduce_factor = 0.0f;
            r_corr *= reduce_factor;
        }
        
        // Relax correction over bright clouds (matching pyspectral):
        // where red-band reflectance >= 0.20, reduce correction linearly.
        if (redband && redband->data_in && !IS_NONDATA(redband->data_in[i])) {
            float rb = redband->data_in[i];
            if (rb >= 0.20f) {
                r_corr *= 1.0f - (rb - 0.20f) / 0.80f;
                if (r_corr < 0.0f) r_corr = 0.0f;
            }
        }
        
        if (r_corr > max_rayleigh) max_rayleigh = r_corr;

        // Apply correction: corrected_reflectance = TOA_reflectance - Rayleigh_path_radiance.
        float val = original - r_corr;

        if (val < 0.0f) {
            val = 0.0f;
            negative_pixels++;
        }

        sum_original += original;
        sum_rayleigh += r_corr;
        sum_corrected += val;
        valid_pixels++;

        img->data_in[i] = val;
    }

    double end_time = omp_get_wtime();
    LOG_TIMING(end_time - start_time, "Rayleigh LUT C%02d (%zu px)", channel, valid_pixels);
    LOG_DEBUG("  noche=%zu clamped=%zu media=%.4f->%.4f corr_max=%.4f",
             night_pixels, negative_pixels,
             valid_pixels > 0 ? sum_original/valid_pixels : 0.0,
             valid_pixels > 0 ? sum_corrected/valid_pixels : 0.0,
             max_rayleigh);

    // Recompute fmin/fmax over corrected data so downstream normalization is correct.
    float new_min = 1e20f;
    float new_max = -1e20f;
    #pragma omp parallel for reduction(min:new_min) reduction(max:new_max)
    for (size_t i = 0; i < n; i++) {
        float val = img->data_in[i];
        if (val > 0.0f && !IS_NONDATA(val)) {
            if (val < new_min) new_min = val;
            if (val > new_max) new_max = val;
        }
    }
    
    if (new_max > new_min) {
        img->fmin = new_min;
        img->fmax = new_max;
        LOG_DEBUG("  Rango post-Rayleigh: [%.6f, %.6f]", new_min, new_max);
    }

    rayleigh_lut_destroy(&lut);
}
