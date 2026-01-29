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

    // Caso 1: Ya coinciden
    if (data->width == target_w && data->height == target_h) return;

    // Caso 2: La navegación es más grande (5000) y queremos pequeña (2500) -> Downsampling
    if (data->width > target_w) {
        int factor = data->width / target_w;
        // Validación básica de factor entero
        if (factor < 1) factor = 1;

        LOG_DEBUG("Ajustando navegación Rayleigh: %dx%d -> %dx%d (factor %d)", 
                 data->width, data->height, target_w, target_h, factor);
        
        // Usamos boxfilter para promediar los ángulos suavemente
        DataF resized = downsample_boxfilter(*data, factor);
        
        // Reemplazamos la matriz original con la redimensionada
        dataf_destroy(data);
        *data = resized;
    }
    // Caso 3: Upsampling (no implementado aquí, raramente necesario para Nav)
}

void rayleigh_free_navigation(RayleighNav *nav) {
    if (nav) {
        dataf_destroy(&nav->sza);
        dataf_destroy(&nav->vza);
        dataf_destroy(&nav->raa);
    }
}

bool rayleigh_load_navigation(const char *filename, RayleighNav *nav, 
				unsigned int target_width, unsigned int target_height) {
    // Inicializar estructura
    nav->sza.data_in = NULL;
    nav->vza.data_in = NULL;
    nav->raa.data_in = NULL;

    LOG_INFO("Generando navegación para Rayleigh (SZA, VZA, RAA)...");

    // 1. Calcular Latitud/Longitud (Necesario para los ángulos)
    DataF navla = {0}, navlo = {0};
    if (compute_navigation_nc(filename, &navla, &navlo) != 0) {
        LOG_ERROR("Falla al computar navegación base (lat/lon).");
        return false;
    }

    // 2. Calcular Ángulos Solares (SZA y SAA)
    DataF saa = {0}; // Solar Azimuth (Temporal)
    if (compute_solar_angles_nc(filename, &navla, &navlo, &nav->sza, &saa) != 0) {
        LOG_ERROR("Falla al computar ángulos solares.");
        dataf_destroy(&navla); dataf_destroy(&navlo);
        return false;
    }

    // 3. Calcular Ángulos del Satélite (VZA y VAA)
    DataF vaa = {0}; // View Azimuth (Temporal)
    if (compute_satellite_angles_nc(filename, &navla, &navlo, &nav->vza, &vaa) != 0) {
        LOG_ERROR("Falla al computar ángulos del satélite.");
        dataf_destroy(&navla); dataf_destroy(&navlo);
        dataf_destroy(&saa);   dataf_destroy(&nav->sza);
        return false;
    }

    // Ya no necesitamos Lat/Lon, liberamos memoria
    dataf_destroy(&navla);
    dataf_destroy(&navlo);

    // 4. Calcular Azimut Relativo (RAA)
    compute_relative_azimuth(&saa, &vaa, &nav->raa);

    // Liberar los azimuts individuales (ya solo necesitamos el relativo)
    dataf_destroy(&saa);
    dataf_destroy(&vaa);

    // Verificación final
    if (!nav->sza.data_in || !nav->vza.data_in || !nav->raa.data_in) {
        rayleigh_free_navigation(nav);
        return false;
    }
    
	if (target_width > 0 && target_height > 0) {
        enforce_resolution(&nav->sza, target_width, target_height);
        enforce_resolution(&nav->vza, target_width, target_height);
        enforce_resolution(&nav->raa, target_width, target_height);

        // Verificar si falló el redimensionado
        if (nav->sza.width != target_width) {
            LOG_ERROR("Falla redimensionando navegación Rayleigh: %dx%d != %dx%d",
                      nav->sza.width, nav->sza.height, target_width, target_height);
            rayleigh_free_navigation(nav);
            return false;
        }
    }    
    return true;
}

// ============================================================================
// SECCIÓN 1: FÍSICA DE RAYLEIGH (Bucholtz 1995)
// ============================================================================

/**
 * @brief Calcula el Espesor Óptico de Rayleigh (Tau) usando Bucholtz (1995).
 * Sustituye a las constantes fijas para mayor precisión espectral.
 * * @param lambda_um Longitud de onda en micrómetros (ej. 0.47 para Azul).
 * @return double Tau_R para presión estándar (1013.25 mb).
 */
static double calc_bucholtz_tau(double lambda_um) {
    if (lambda_um <= 0) return 0.0;
    
    double l2 = lambda_um * lambda_um;
    double l4 = l2 * l2;
    
    // Fórmula de aproximación de alta precisión para Tau (P0=1013.25mb)
    // Coeficientes derivados de Bucholtz (1995) para atmósfera estándar.
    // Tau = A * lambda^-4 * (1 + B/lambda^2 + C/lambda^4)
    return 0.008569 / l4 * (1.0 + 0.0113 / l2 + 0.00013 / l4);
}

/**
 * @brief Función de Fase de Rayleigh con corrección de Despolarización.
 * Considera que las moléculas de aire no son esferas perfectas (Factor 0.0279).
 * * @param cos_theta Coseno del ángulo de dispersión.
 * @return float Valor de la función de fase P(Theta).
 */
static float calc_bucholtz_phase(float cos_theta) {
    // Factor de despolarización (rho_n) para aire estándar
    const float rho_n = 0.0279f; 
    const float gamma = rho_n / (2.0f - rho_n);
    
    // Términos precalculados de la ecuación de Chandrasekhar
    const float A = 0.75f / (1.0f + 2.0f * gamma);
    const float B = 1.0f + 3.0f * gamma;
    const float C = 1.0f - gamma;

    // P(Theta) = A * [ B + C * cos^2(Theta) ]
    return A * (B + C * (cos_theta * cos_theta));
}

// ============================================================================
// SECCIÓN 3: CORRECCIÓN ANALÍTICA MEJORADA
// ============================================================================

void analytic_rayleigh_correction(DataF *band, const RayleighNav *nav, float lambda_um) {
    // 1. Validaciones básicas de punteros
    if (!band || !nav) {
        LOG_ERROR("Argumentos nulos en analytic_rayleigh_correction");
        return;
    }
	DataF *output = band;
	
    // Extraer punteros de la estructura de navegación para facilitar el acceso
    // NOTA: Asumo que en RayleighNav son punteros (DataF *sza). 
    // Si son estructuras directas, usa &nav->sza.
    const DataF *sza = &nav->sza;
    const DataF *vza = &nav->vza;
    const DataF *raa = &nav->raa; 

    // Validar que los datos de navegación existan
    if (!sza || !vza || !raa || !sza->data_in || !vza->data_in || !raa->data_in) {
        LOG_ERROR("Datos de navegación incompletos en RayleighNav");
        return;
    }

    // Validar dimensiones (asumiendo que enforce_resolution ya se llamó externamente o antes)
    if (sza->size != band->size) {
        LOG_WARN("Dimensiones de navegación (%zu) no coinciden con banda (%zu). Resultados impredecibles.", 
                    sza->size, band->size);
        // Aquí podrías llamar a enforce_resolution si fuera necesario
    }

    size_t n = band->size;
    
    // 2. Calcular Tau físico
    float tau_r = (float)calc_bucholtz_tau(lambda_um);
    
    LOG_INFO("Rayleigh (Bucholtz): Lambda=%.3f um, Tau=%.4f", lambda_um, tau_r);

    size_t night_pixels = 0;
    size_t valid_pixels = 0;
    size_t clamped_pixels = 0;
    double sum_orig = 0, sum_corr = 0;

    #pragma omp parallel for reduction(+:night_pixels, valid_pixels, clamped_pixels, sum_orig, sum_corr)
    for (size_t i = 0; i < n; i++) {
        float val = band->data_in[i];

        if (IS_NONDATA(val)) {
            output->data_in[i] = NonData;
            continue;
        }

        // Acceso directo a los arrays de navegación
        float sza_val = sza->data_in[i];
        
        if (sza_val > 85.0f) {
             output->data_in[i] = NonData; 
             night_pixels++;
             continue;
        }

        float vza_val = vza->data_in[i];
        
        // Conversión a radianes
        float theta_s = sza_val * (float)(M_PI / 180.0);
        float theta_v = vza_val * (float)(M_PI / 180.0);
        float phi_rel = raa->data_in[i] * (float)(M_PI / 180.0);

        float mu_s = cosf(theta_s);
        float mu_v = cosf(theta_v);

        if (mu_s < 0.01f || mu_v < 0.01f) {
            output->data_in[i] = val;
            continue;
        }

        // Geometría
        float cos_scat = -mu_s * mu_v + sinf(theta_s) * sinf(theta_v) * cosf(phi_rel);

        // Fase y Reflectancia
        float P_ray = calc_bucholtz_phase(cos_scat);
        float rho_ray = (tau_r * P_ray) / (4.0f * mu_s * mu_v);

        // Corrección
        float corrected = val - rho_ray;

        sum_orig += val;
        valid_pixels++;

        if (corrected < 0.0f) {
            corrected = 0.0001f;
            clamped_pixels++;
        }
        sum_corr += corrected;

        output->data_in[i] = corrected;
    }

    if (valid_pixels > 0) {
        LOG_INFO("Rayleigh Stats: %zu valid. Mean: %.4f -> %.4f. Clamped: %.1f%%",
            valid_pixels, sum_orig/valid_pixels, sum_corr/valid_pixels, 
            100.0 * (double)clamped_pixels / valid_pixels);
    }
}


/*********         Implementación Rayleigh con LUTs          **********/

/**
 * Interpola valor de Rayleigh desde la LUT usando secantes.
 * Realiza una interpolación trilineal rápida sobre la LUT.
 * s: Solar Zenith Secant, v: View Zenith Secant, a: Relative Azimuth (degrees)
 */
static inline float get_rayleigh_value(const RayleighLUT *lut, float s, float v, float a) {
    // 1. Validar rangos - clamp en lugar de devolver 0
    if (s < lut->sz_min) s = lut->sz_min; // Clampeo inferior
    if (s >= lut->sz_max) s = lut->sz_max; // Clampeo superior
    
    // Para VZA fuera de rango, hacer clamping (extrapolación constante)
    if (v < lut->vz_min) v = lut->vz_min; // Clampeo inferior
    if (v >= lut->vz_max) v = lut->vz_max; // Clampeo superior
    
    // Asegurar que el azimut esté entre 0 y 180 (simetría)
    a = fabsf(a);
    if (a > 180.0f) a = 360.0f - a;
    if (a > lut->az_max) a = lut->az_max;
    if (a < lut->az_min) a = lut->az_min;

    // 2. Calcular índices flotantes (usando secantes directamente)
    float idx_s = (s - lut->sz_min) / lut->sz_step;
    float idx_v = (v - lut->vz_min) / lut->vz_step;
    float idx_a = (a - lut->az_min) / lut->az_step;

    // 3. Índices enteros base (vecino inferior)
    int s0 = (int)idx_s;
    int v0 = (int)idx_v;
    int a0 = (int)idx_a;

    // Índices del vecino superior
    // Asegurar que los índices superiores no se salgan de los límites del array
    int s1 = s0 + 1; 
    if (s1 >= lut->n_sz) s1 = lut->n_sz - 1;

    int v1 = v0 + 1;
    if (v1 >= lut->n_vz) v1 = lut->n_vz - 1;

    int a1 = a0 + 1;
    if (a1 >= lut->n_az) a1 = lut->n_az - 1;

    // 4. Fracciones para la ponderación (deltas)
    float ds = idx_s - s0;
    float dv = idx_v - v0;
    float da = idx_a - a0;

    // 5. Calcular strides para el acceso al array plano 1D
    // Asumiendo orden de datos [SolarZenith][ViewZenith][Azimuth]
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

    // 7. Interpolación Trilineal
    // Interpolar en eje Azimuth
    float c00 = c000 * (1.0f - da) + c001 * da; // Borde inferior de S y V
    float c01 = c010 * (1.0f - da) + c011 * da; // Borde inferior de S, superior de V
    float c10 = c100 * (1.0f - da) + c101 * da; // Borde superior de S, inferior de V
    float c11 = c110 * (1.0f - da) + c111 * da; // Borde superior de S y V

    // Interpolar en eje View Zenith
    float c0 = c00 * (1.0f - dv) + c01 * dv;
    float c1 = c10 * (1.0f - dv) + c11 * dv;

    // Interpolar en eje Solar Zenith
    float result = c0 * (1.0f - ds) + c1 * ds;
    
    // Las LUTs de pyspectral ya contienen valores de reflectancia directos (0-100)
    // No necesitan dividirse por 100, están en porcentaje directo
    // Multiplicaremos por tau en la función que llama
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
    // Determinar qué LUT embebida usar
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
    
    // Calcular estadísticas de la tabla para verificar el rango
    float min_val = lut.table[0];
    float max_val = lut.table[0];
    double sum = 0.0;
    for (size_t i = 0; i < table_size; i++) {
        float v = lut.table[i];
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
        sum += v;
    }
    float mean_val = (float)(sum / table_size);
    
    LOG_INFO("LUT de Rayleigh %d cargada desde datos embebidos", channel);
    LOG_INFO("  Dimensiones: %d × %d × %d = %zu valores", 
             lut.n_sz, lut.n_vz, lut.n_az, table_size);
    LOG_INFO("  Solar Zenith Secant: %.2f - %.2f (step: %.3f)", 
             lut.sz_min, lut.sz_max, lut.sz_step);
    LOG_INFO("  View Zenith Secant: %.2f - %.2f (step: %.3f)", 
             lut.vz_min, lut.vz_max, lut.vz_step);
    LOG_INFO("  Azimuth: %.0f° - %.0f° (step: %.1f°)", 
             lut.az_min, lut.az_max, lut.az_step);
    LOG_INFO("  Valores tabla: min=%.6f, max=%.6f, media=%.6f", 
             min_val, max_val, mean_val);
    
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


void luts_rayleigh_correction(DataF *img, const RayleighNav *nav, const uint8_t channel, float tau) {
	// Validar dimensiones
    if (img->width != nav->sza.width || img->height != nav->sza.height) {
        LOG_ERROR("Mismatch dimensiones en Rayleigh Analytic: Img %dx%d vs Nav %dx%d",
                  img->width, img->height, nav->sza.width, nav->sza.height);
        return;
    }
    RayleighLUT lut = rayleigh_lut_load_from_memory(channel);

    size_t n = img->size;
    double start_time = omp_get_wtime();
    
    // Estadísticas para debugging
    size_t night_pixels = 0;
    size_t negative_pixels = 0;
    size_t valid_pixels = 0;
    double sum_original = 0.0;
    double sum_rayleigh = 0.0;
    double sum_corrected = 0.0;
    float max_rayleigh = 0.0f;
    float min_original = 1e9;
    float max_original = -1e9;

    // Paralelización con OpenMP
    // 'schedule(static)' es eficiente porque la carga de trabajo por píxel es constante
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

        // Optimización: Si el cenit solar es muy alto (noche/crepúsculo), saltar
        // Usamos 88° en lugar de 85° para permitir corrección en crepúsculo
        if (theta_s > 88.0f || IS_NONDATA(theta_s) || theta_s < 0.0f) {
            img->data_in[i] = 0.0f; // Enmascarar noche
            night_pixels++;
            continue;
        }

        // Calcular secantes para interpolación (como hace pyspectral)
        // Primero clipear ángulos al rango válido de la LUT (como hace pyspectral)
        // SZA max = arccos(1/24.75) = 87.68°
        // VZA max = arccos(1/3.0) = 70.53°
        float sza_clipped = theta_s;
        if (sza_clipped > 87.68f) sza_clipped = 87.68f;
        if (sza_clipped < 0.0f) sza_clipped = 0.0f;
        
        float vza_clipped = nav->vza.data_in[i];
        if (vza_clipped > 70.53f) vza_clipped = 70.53f;
        if (vza_clipped < 0.0f) vza_clipped = 0.0f;
        
        float theta_s_sec = 1.0f / cosf(sza_clipped * M_PI / 180.0f);
        float vza_sec = 1.0f / cosf(vza_clipped * M_PI / 180.0f);
        
        // Interpolar en LUT usando secantes
        float r_corr = get_rayleigh_value(&lut, theta_s_sec, vza_sec, nav->raa.data_in[i]);
        
        // Reducir corrección en ángulos solares altos (como hace satpy/pyspectral)
        // Para SZA > 70°, reduce linealmente hasta eliminar la corrección en SZA=88°
        // Esto evita sobrecorrecciones irreales en el terminador día/noche
        if (theta_s > 70.0f) {
            float reduce_factor = 1.0f - (theta_s - 70.0f) / (88.0f - 70.0f);
            if (reduce_factor < 0.0f) reduce_factor = 0.0f;
            r_corr *= reduce_factor;
        }
        
        if (r_corr > max_rayleigh) max_rayleigh = r_corr;
        
        // Debug: samplear algunos valores distribuidos por la imagen
        if (valid_pixels % 10000 == 0 && valid_pixels < 100000) {
            #pragma omp critical
            {
                LOG_DEBUG("Sample pixel %zu: SZA=%.2f (clipped=%.2f), VZA=%.2f (clipped=%.2f), RAA=%.2f, Original=%.6f, Rayleigh=%.6f", 
                         i, theta_s, sza_clipped, nav->vza.data_in[i], vza_clipped, nav->raa.data_in[i], original, r_corr);
            }
        }

        // Aplicar corrección: Reflectancia = Observada - Rayleigh
        float val = original - r_corr;

        // Clamping: Evitar valores negativos que ensucian la imagen
        if (val < 0.0f) {
            val = 0.0f;
            negative_pixels++;
        }

        sum_original += original;
        sum_rayleigh += r_corr;
        sum_corrected += val;
        valid_pixels++;

        // Guardar resultado
        img->data_in[i] = val;
    }

    double end_time = omp_get_wtime();
    LOG_INFO("Kernel de corrección de Rayleigh completado en %.4f segundos.", end_time - start_time);
    LOG_INFO("Estadísticas de corrección:");
    LOG_INFO("  Píxeles noche (SZA>85°):    %zu (%.1f%%)", night_pixels, 100.0*night_pixels/n);
    LOG_INFO("  Píxeles válidos corregidos: %zu (%.1f%%)", valid_pixels, 100.0*valid_pixels/n);
    LOG_INFO("  Píxeles negativos clamped:  %zu (%.1f%%)", negative_pixels, 100.0*negative_pixels/n);
    if (valid_pixels > 0) {
        LOG_INFO("  Reflectancia original:  min=%.6f, max=%.6f, media=%.6f", 
                 min_original, max_original, sum_original/valid_pixels);
        LOG_INFO("  Corrección Rayleigh:    max=%.6f, media=%.6f", 
                 max_rayleigh, sum_rayleigh/valid_pixels);
        LOG_INFO("  Reflectancia corregida: media=%.6f", sum_corrected/valid_pixels);
    }

    // Actualizar fmin/fmax de la estructura para que la normalización sea correcta
    // Recalcular min/max sobre los datos corregidos
    float new_min = 1e20f;
    float new_max = -1e20f;
    for (size_t i = 0; i < n; i++) {
        float val = img->data_in[i];
        // Solo considerar píxeles válidos: mayor que 0 y no NonData
        if (val > 0.0f && !IS_NONDATA(val)) {
            if (val < new_min) new_min = val;
            if (val > new_max) new_max = val;
        }
    }
    
    if (new_max > new_min) {
        img->fmin = new_min;
        img->fmax = new_max;
        LOG_INFO("  Rango actualizado después de Rayleigh: [%.6f, %.6f]", new_min, new_max);
    }
}
