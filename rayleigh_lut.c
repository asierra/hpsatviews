// rayleigh_lut.c
// Implementación de la carga de LUTs de CSPP GEO (Aerosol + Sunglint)

#include "rayleigh_lut.h"
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Asumo que tienes un logger.h, si no, reemplaza LOG_ERROR/LOG_WARN con fprintf(stderr, ...)
#include "logger.h" 

#define ERRCODE 2
#define ERR(e) { LOG_ERROR("Error NetCDF: %s", nc_strerror(e)); return 0; }
#define WRN(e) { LOG_WARN("Advertencia NetCDF: %s", nc_strerror(e)); }


// ====================================================================
// FUNCIONES AUXILIARES DE CARGA (Helpers)
// ====================================================================

/**
 * Carga un array 1D de flotantes (para los nudos).
 * Devuelve 1 en éxito, 0 en fallo.
 */
static int nc_load_lut_array_1d(int ncid, const char *varname, float **data_out, size_t *size_out) {
    int retval, varid, dimid, ndims;
    
    if ((retval = nc_inq_varid(ncid, varname, &varid))) ERR(retval);
    if ((retval = nc_inq_varndims(ncid, varid, &ndims))) ERR(retval);
    if (ndims != 1) {
        LOG_ERROR("Error: %s no es 1D.", varname);
        return 0;
    }
    
    if ((retval = nc_inq_vardimid(ncid, varid, &dimid))) ERR(retval);
    if ((retval = nc_inq_dimlen(ncid, dimid, size_out))) ERR(retval);

    *data_out = (float*)malloc(*size_out * sizeof(float));
    if (!*data_out) {
        LOG_ERROR("Fallo de memoria al cargar %s", varname);
        return 0;
    }

    if ((retval = nc_get_var_float(ncid, varid, *data_out))) {
        free(*data_out);
        *data_out = NULL;
        ERR(retval);
    }
    return 1;
}


/**
 * Carga y "rebana" (slice) un array 4D/5D para obtener una grilla 3D.
 * Usado para 'ray_path_refl' (Lp_0).
 * Asume que las dimensiones son [NChn, (NAer), SZA, VZA, RA]
 * Devuelve 1 en éxito, 0 en fallo.
 */
static int nc_load_lut_slice_3d(int ncid, const char *varname, int band_idx, 
                                size_t sza_dim, size_t vza_dim, size_t ra_dim, 
                                float **data_out) {
    int retval, varid, ndims;
    size_t start[NC_MAX_VAR_DIMS] = {0};
    size_t count[NC_MAX_VAR_DIMS];
    size_t expected_size = sza_dim * vza_dim * ra_dim;

    if ((retval = nc_inq_varid(ncid, varname, &varid))) ERR(retval);
    if ((retval = nc_inq_varndims(ncid, varid, &ndims))) ERR(retval);

    *data_out = (float *)malloc(expected_size * sizeof(float));
    if (!*data_out) {
        LOG_ERROR("Fallo de memoria al rebanar %s", varname);
        return 0;
    }

    // El índice de banda GOES (1, 2, 3) se mapea al índice NetCDF (0, 1, 2)
    size_t channel_index = (size_t)band_idx - 1;

    if (ndims == 5) { // Asume [NChn, NAer, SZA, VZA, RA]
        start[0] = channel_index; count[0] = 1;
        start[1] = 0;             count[1] = 1; // Modelo Aerosol 0 (Rayleigh)
        start[2] = 0;             count[2] = sza_dim;
        start[3] = 0;             count[3] = vza_dim;
        start[4] = 0;             count[4] = ra_dim;
    } else if (ndims == 4) { // Asume [NChn, SZA, VZA, RA]
        start[0] = channel_index; count[0] = 1;
        start[1] = 0;             count[1] = sza_dim;
        start[2] = 0;             count[2] = vza_dim;
        start[3] = 0;             count[3] = ra_dim;
    } else {
        LOG_ERROR("%s tiene dimensiones inesperadas (%d)", varname, ndims);
        free(*data_out);
        return 0;
    }

    if ((retval = nc_get_vara_float(ncid, varid, start, count, *data_out))) {
        free(*data_out);
        *data_out = NULL;
        ERR(retval);
    }
    return 1;
}


/**
 * Carga un array 2D y lo "expande" a 3D.
 * Usado para 'ray_trans' (T_up, E_down), que es [NChn, NSolZenAng].
 * Asume que el valor es constante para VZA y RA.
 */
static int nc_load_and_expand_2d(int ncid, const char *varname, int band_idx, 
                                 size_t sza_dim, size_t vza_dim, size_t ra_dim, 
                                 float **data_out) {
    int retval, varid;
    size_t start[2] = {0};
    size_t count[2] = {0};
    size_t component_size_3d = sza_dim * vza_dim * ra_dim;

    // 1. Crear un buffer temporal para el slice 1D (de tamaño SZA)
    float *sza_slice_1d = (float*)malloc(sza_dim * sizeof(float));
    if (!sza_slice_1d) {
        LOG_ERROR("Fallo de memoria para buffer 2D expandido");
        return 0;
    }

    // 2. Leer el slice 1D de la variable 2D [NChn, SZA]
    if ((retval = nc_inq_varid(ncid, varname, &varid))) { free(sza_slice_1d); ERR(retval); }
    
    start[0] = (size_t)band_idx - 1; // Índice de Canal
    count[0] = 1;
    start[1] = 0;                    // Índice SZA
    count[1] = sza_dim;
    
    if ((retval = nc_get_vara_float(ncid, varid, start, count, sza_slice_1d))) {
        free(sza_slice_1d);
        ERR(retval);
    }

    // 3. Asignar memoria para el array 3D final
    *data_out = (float *)malloc(component_size_3d * sizeof(float));
    if (!*data_out) {
        LOG_ERROR("Fallo de memoria para expansión 2D->3D");
        free(sza_slice_1d);
        return 0;
    }
    
    // 4. Expandir/Rellenar el array 3D
    // El valor solo cambia con SZA (índice i)
    for (size_t i = 0; i < sza_dim; i++) {
        float val = sza_slice_1d[i]; // Valor para este SZA
        for (size_t j = 0; j < vza_dim; j++) {
            for (size_t k = 0; k < ra_dim; k++) {
                size_t idx_3d = i * (vza_dim * ra_dim) + j * ra_dim + k;
                (*data_out)[idx_3d] = val;
            }
        }
    }
    
    free(sza_slice_1d);
    return 1;
}


/**
 * Carga un array 1D y lo "expande" a 3D.
 * Usado para 'ray_sph_alb' (S_alb), que es [NChn].
 * Asume que el valor es constante para SZA, VZA y RA.
 */
static int nc_load_and_expand_1d(int ncid, const char *varname, int band_idx, 
                                 size_t component_size_3d, float **data_out) {
    int retval, varid;
    size_t start[1] = {0};
    size_t count[1] = {1};
    float value_1d; // Solo leemos un flotante

    if ((retval = nc_inq_varid(ncid, varname, &varid))) ERR(retval);

    start[0] = (size_t)band_idx - 1; // Índice de Canal

    if ((retval = nc_get_vara_float(ncid, varid, start, count, &value_1d))) ERR(retval);

    // Asignar memoria para el array 3D final
    *data_out = (float *)malloc(component_size_3d * sizeof(float));
    if (!*data_out) {
        LOG_ERROR("Fallo de memoria para expansión 1D->3D");
        return 0;
    }

    // Rellenar todo el array 3D con este valor único
    for (size_t i = 0; i < component_size_3d; i++) {
        (*data_out)[i] = value_1d;
    }
    return 1;
}


// ====================================================================
// FUNCIÓN PRINCIPAL DE CARGA DE LUT (Orquestador)
// ====================================================================

RayleighLUT lut_load_for_band(int band_id) {
    RayleighLUT lut;
    memset(&lut, 0, sizeof(RayleighLUT)); 
    int ncid_knots = 0, ncid_data = 0;
    float *rtc_buffers[NUM_COMPONENTS] = {NULL}; // Buffers temporales para los 4 RTCs

    // --- Definición de Rutas y Nombres de Variables ---
    // (Debes reemplazar estas rutas por las de tu sistema)
    const char *knots_path = "/data/cspp/abi_luts/ABI_Sunglint_Lut_G19.nc";
    const char *data_path = "/data/cspp/abi_luts/ABI_Aerosol_Lut_G19.nc";

    // Nombres de los nudos (knots) en el archivo Sunglint
    const char *knot_vars[3] = {
        "solar_zenith_angle",  // SZA
        "sensor_zenith_angle", // VZA
        "relative_azimuth_angle" // RA
    };

    // Nombres de los RTCs en el archivo Aerosol
    const char *rtc_vars[NUM_COMPONENTS] = {
        "ray_path_refl", // 4D/5D (Lp_0)
        "ray_trans",     // 2D (T_up) - Usaremos el mismo para ambos
        "ray_trans",     // 2D (E_down)
        "ray_sph_alb"    // 1D (S_alb)
    };
    
    // 1. ABRIR Y CARGAR NUDOS (Knots) del archivo Sunglint
    LOG_INFO("Cargando nudos (knots) desde %s", knots_path);
    if (nc_open(knots_path, NC_NOWRITE, &ncid_knots)) goto cleanup;
    
    if (!nc_load_lut_array_1d(ncid_knots, knot_vars[0], &lut.sza_knots, &lut.sza_dim)) goto cleanup;
    if (!nc_load_lut_array_1d(ncid_knots, knot_vars[1], &lut.vza_knots, &lut.vza_dim)) goto cleanup;
    if (!nc_load_lut_array_1d(ncid_knots, knot_vars[2], &lut.ra_knots, &lut.ra_dim)) goto cleanup;
    
    nc_close(ncid_knots); ncid_knots = 0; // Cerrar inmediatamente
    
    // 2. CALCULAR TAMAÑOS
    lut.component_size = lut.sza_dim * lut.vza_dim * lut.ra_dim;
    size_t total_lut_size = lut.component_size * NUM_COMPONENTS;
    if (lut.component_size == 0) goto cleanup;

    // 3. ABRIR Y CARGAR DATOS RTC del archivo Aerosol
    LOG_INFO("Cargando datos RTC (Rayleigh) desde %s", data_path);
    if (nc_open(data_path, NC_NOWRITE, &ncid_data)) goto cleanup;

    // Cargar los 4 RTCs usando las funciones helper
    // Lp_0 (4D/5D)
    if (!nc_load_lut_slice_3d(ncid_data, rtc_vars[0], band_id, 
                              lut.sza_dim, lut.vza_dim, lut.ra_dim, 
                              &rtc_buffers[0])) goto cleanup;
    
    // T_up (2D)
    if (!nc_load_and_expand_2d(ncid_data, rtc_vars[1], band_id, 
                               lut.sza_dim, lut.vza_dim, lut.ra_dim, 
                               &rtc_buffers[1])) goto cleanup;
    
    // E_down (2D) - Reutilizamos el mismo
    if (!nc_load_and_expand_2d(ncid_data, rtc_vars[2], band_id, 
                               lut.sza_dim, lut.vza_dim, lut.ra_dim, 
                               &rtc_buffers[2])) goto cleanup;
    
    // S_alb (1D)
    if (!nc_load_and_expand_1d(ncid_data, rtc_vars[3], band_id, 
                               lut.component_size, 
                               &rtc_buffers[3])) goto cleanup;
    
    nc_close(ncid_data); ncid_data = 0; // Cerrar inmediatamente

    // 4. ASIGNAR Y CONCATENAR EL BUFFER FINAL
    lut.data = (float *)malloc(total_lut_size * sizeof(float));
    if (!lut.data) {
        LOG_ERROR("Fallo de memoria para el buffer LUT final");
        goto cleanup;
    }

    size_t current_offset = 0;
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        memcpy(lut.data + current_offset, rtc_buffers[i], lut.component_size * sizeof(float));
        current_offset += lut.component_size;
    }
    
    LOG_INFO("LUT para banda %d cargada exitosamente.", band_id);
    
    // 5. LIMPIEZA DE BUFFERS TEMPORALES
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        if (rtc_buffers[i]) free(rtc_buffers[i]);
    }
    
    return lut;

cleanup:
    LOG_ERROR("FALLO al cargar la LUT para banda %d.", band_id);
    if (ncid_knots > 0) nc_close(ncid_knots);
    if (ncid_data > 0) nc_close(ncid_data);
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        if (rtc_buffers[i]) free(rtc_buffers[i]);
    }
    // lut_destroy liberará los nudos si se cargaron
    lut_destroy(&lut); 
    return (RayleighLUT){0}; // Retorna una estructura vacía
}


/**
 * Libera toda la memoria asignada para la estructura LUT.
 */
void lut_destroy(RayleighLUT *lut) {
    if (lut) {
        if (lut->data) free(lut->data);
        if (lut->sza_knots) free(lut->sza_knots);
        if (lut->vza_knots) free(lut->vza_knots);
        if (lut->ra_knots) free(lut->ra_knots);
        // Poner la estructura a cero para evitar doble liberación
        memset(lut, 0, sizeof(RayleighLUT)); 
    }
}
