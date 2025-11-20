#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>
#include "datanc.h"
#include "logger.h"
#include "rayleigh.h"

/**
 * Realiza una interpolación trilineal rápida sobre la LUT.
 * s: Solar Zenith, v: View Zenith, a: Relative Azimuth
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

    // 2. Calcular índices flotantes
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
    
    // CRÍTICO: Las LUTs de pyspectral están en escala 0-100, necesitamos dividir por 100
    // para convertir a reflectancia 0-1
    return result / 100.0f;
}

/**
 * Aplica corrección Rayleigh modificando 'img' in-place.
 * img: DataF con reflectancia TOA (input/output)
 * sza: Solar Zenith Angle
 * vza: View Zenith Angle (Satellite Zenith)
 * raa: Relative Azimuth Angle
 * lut: Puntero a la tabla cargada
 */
void apply_rayleigh_correction(DataF *img, const DataF *sza, const DataF *vza, const DataF *raa, const RayleighLUT *lut) {
    if (img->size != sza->size || img->size != vza->size || img->size != raa->size) {
        LOG_ERROR("Dimensiones de geometría no coinciden con la imagen en hpsat_rayleigh_correct.");
        LOG_ERROR("  Imagen:  %zux%zu = %zu píxeles", img->width, img->height, img->size);
        LOG_ERROR("  SZA:     %zux%zu = %zu píxeles", sza->width, sza->height, sza->size);
        LOG_ERROR("  VZA:     %zux%zu = %zu píxeles", vza->width, vza->height, vza->size);
        LOG_ERROR("  RAA:     %zux%zu = %zu píxeles", raa->width, raa->height, raa->size);
        return;
    }

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
        float theta_s = sza->data_in[i];
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

        // Calcular valor Rayleigh de la tabla
        float r_corr = get_rayleigh_value(lut, theta_s, vza->data_in[i], raa->data_in[i]);
        if (r_corr > max_rayleigh) max_rayleigh = r_corr;
        
        // Debug: samplear algunos valores distribuidos por la imagen
        if (valid_pixels % 10000 == 0 && valid_pixels < 100000) {
            #pragma omp critical
            {
                LOG_DEBUG("Sample pixel %zu: SZA=%.2f, VZA=%.2f, RAA=%.2f, Original=%.6f, Rayleigh=%.6f", 
                         i, theta_s, vza->data_in[i], raa->data_in[i], original, r_corr);
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

/**
 * Carga una LUT de Rayleigh desde un archivo binario.
 * 
 * Formato del archivo:
 * - Header (48 bytes): 9 floats (min, max, step) + 3 ints (dimensiones)
 * - Data: Array 3D float32 [sza][vza][azimuth]
 * 
 * @param filename Ruta al archivo binario (.bin)
 * @return Estructura RayleighLUT cargada (table será NULL si falla)
 */
RayleighLUT rayleigh_lut_load(const char *filename) {
    RayleighLUT lut = {0};
    
    FILE *f = fopen(filename, "rb");
    if (!f) {
        LOG_ERROR("No se pudo abrir LUT de Rayleigh: %s", filename);
        return lut;
    }
    
    // Leer header (48 bytes: 9 floats + 3 ints)
    size_t items_read = 0;
    items_read += fread(&lut.sz_min, sizeof(float), 1, f);
    items_read += fread(&lut.sz_max, sizeof(float), 1, f);
    items_read += fread(&lut.sz_step, sizeof(float), 1, f);
    items_read += fread(&lut.vz_min, sizeof(float), 1, f);
    items_read += fread(&lut.vz_max, sizeof(float), 1, f);
    items_read += fread(&lut.vz_step, sizeof(float), 1, f);
    items_read += fread(&lut.az_min, sizeof(float), 1, f);
    items_read += fread(&lut.az_max, sizeof(float), 1, f);
    items_read += fread(&lut.az_step, sizeof(float), 1, f);
    items_read += fread(&lut.n_sz, sizeof(int), 1, f);
    items_read += fread(&lut.n_vz, sizeof(int), 1, f);
    items_read += fread(&lut.n_az, sizeof(int), 1, f);
    
    if (items_read != 12) {
        LOG_ERROR("Error al leer header de LUT: %s", filename);
        fclose(f);
        return lut;
    }
    
    // Validar dimensiones
    if (lut.n_sz <= 0 || lut.n_vz <= 0 || lut.n_az <= 0 ||
        lut.n_sz > 1000 || lut.n_vz > 1000 || lut.n_az > 1000) {
        LOG_ERROR("Dimensiones inválidas en LUT: %dx%dx%d", lut.n_sz, lut.n_vz, lut.n_az);
        fclose(f);
        lut.n_sz = lut.n_vz = lut.n_az = 0;
        return lut;
    }
    
    // Alocar memoria para la tabla
    size_t table_size = (size_t)lut.n_sz * lut.n_vz * lut.n_az;
    lut.table = malloc(table_size * sizeof(float));
    if (!lut.table) {
        LOG_ERROR("Falla de memoria al alocar LUT (%zu valores)", table_size);
        fclose(f);
        lut.n_sz = lut.n_vz = lut.n_az = 0;
        return lut;
    }
    
    // Leer datos
    size_t values_read = fread(lut.table, sizeof(float), table_size, f);
    fclose(f);
    
    if (values_read != table_size) {
        LOG_ERROR("Error al leer datos de LUT: esperados %zu, leídos %zu", table_size, values_read);
        free(lut.table);
        lut.table = NULL;
        lut.n_sz = lut.n_vz = lut.n_az = 0;
        return lut;
    }
    
    LOG_INFO("LUT de Rayleigh cargada: %s", filename);
    LOG_INFO("  Dimensiones: %d × %d × %d = %zu valores", 
             lut.n_sz, lut.n_vz, lut.n_az, table_size);
    LOG_INFO("  Solar Zenith: %.1f° - %.1f° (step: %.2f°)", 
             lut.sz_min, lut.sz_max, lut.sz_step);
    LOG_INFO("  View Zenith: %.1f° - %.1f° (step: %.2f°)", 
             lut.vz_min, lut.vz_max, lut.vz_step);
    LOG_INFO("  Azimuth: %.0f° - %.0f° (step: %.1f°)", 
             lut.az_min, lut.az_max, lut.az_step);
    
    return lut;
}

/**
 * Libera la memoria de una LUT de Rayleigh.
 * 
 * @param lut Puntero a la estructura RayleighLUT a liberar
 */
void rayleigh_lut_destroy(RayleighLUT *lut) {
    if (lut && lut->table) {
        free(lut->table);
        lut->table = NULL;
        lut->n_sz = lut->n_vz = lut->n_az = 0;
        LOG_DEBUG("LUT de Rayleigh liberada.");
    }
}