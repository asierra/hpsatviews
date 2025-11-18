/*
 * rayleigh_correction.c
 * Implementación completa de la corrección de Rayleigh para GOES L1b
 * usando el "Modelo Híbrido".
 */

// Cabeceras estándar
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <netcdf.h> // Para la carga de LUTs
#include <omp.h>    // Para paralelismo

// Cabeceras de tu proyecto
#include "datanc.h"
#include "image.h"
#include "logger.h"
#include "reader_nc.h" 
#include "writer_png.h" 

// ====================================================================
// ESTRUCTURAS Y DEFINICIONES
// ====================================================================

// Definición de la LUT (debe estar en un .h compartido)
typedef enum { LP_0_COMP, T_UP_COMP, E_DOWN_COMP, S_ALB_COMP, NUM_COMPONENTS } LutComponent;
typedef struct {
    float *data;
    size_t sza_dim, vza_dim, ra_dim;
    size_t component_size;
    float *sza_knots, *vza_knots, *ra_knots;
} RayleighLUT;


// ====================================================================
// PROTOTIPOS DE FUNCIONES (Implementadas en este archivo)
// ====================================================================

// Funciones de carga de LUT
RayleighLUT lut_load_for_band(int band_id, const char *knots_path, const char *data_path);
void lut_destroy(RayleighLUT *lut);

// Funciones de Geometría
void sun_geometry(float la, float lo, DataNC datanc, double *angle, double *zenith_out, double *azimuth_out); //
void create_solar_maps(const DataNC *l1b_data, const DataF *navla, const DataF *navlo, 
                       DataF *sza_map_out, DataF *saa_map_out);
int create_relative_azimuth_map(const DataF *vaa_in, const DataF *saa_in, DataF *ra_map_out);

// Funciones de Corrección
static float interpolate_trilinear(const RayleighLUT *lut, float sza_val, float vza_val, float ra_val, LutComponent component);
void apply_rayleigh_correction(const DataF *L_TO_A, const DataF *sza_map, 
                               const DataF *vza_map, const DataF *ra_map, 
                               const RayleighLUT *lut, DataF *rho_surface_out);

// Función de RGB
ImageData create_truecolor_rgb(DataF rho_c01, DataF rho_c02, DataF rho_c03);


// ====================================================================
// FUNCIÓN MAIN (Orquestador Principal - Tu esqueleto actualizado)
// ====================================================================

int main(int argc, char *argv[]) {
    // Inicializar el logger
    logger_init(LOG_INFO); // O el nivel que prefieras

    // 1. DEFINICIÓN DE RUTAS (¡¡¡ ACTUALIZA ESTAS RUTAS !!!)
    const char *l1b_path_c01 = "/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20253141600218_e20253141609526_c20253141609573.nc";
    const char *l1b_path_c02 = "/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C02_G19_s20253141600218_e20253141609526_c20253141609557.nc";
    const char *l1b_path_c03 = "/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C03_G19_s20253141600218_e20253141609526_c20253141609569.nc";
    
    // Archivo de Navegación (Geometría Estática)
    const char *nav_path = "/data/output/abi/l2/fd/CG_ABI-L2-NAVF-M6_G19_s20253141550218_e20253141559538_c20253141612370.nc";

    // Archivos LUT (Geometría de Corrección)
    const char *lut_path_sunglint = "/data/cspp/abi_luts/ABI_Sunglint_Lut_G19.nc";
    const char *lut_path_aerosol = "/data/cspp/abi_luts/ABI_Aerosol_Lut_G19.nc";

    // 2. CARGA DE DATOS L1B (Radiancias y Tiempo)
    LOG_INFO("Paso 1: Cargando Radiancias L1b...");
    DataNC c01_nc; // Declara primero
    // Usa tu función 'load_nc_sf' para cargar la banda 1 Y los metadatos de tiempo
    if (load_nc_sf(l1b_path_c01, "Rad", &c01_nc) != 0) {
        LOG_FATAL("Falla al cargar L1b C01. Terminando.");
        return 1;
    }

    // Usa la NUEVA función 'dataf_load_from_netcdf' para las otras bandas
    DataF rad_c02 = dataf_load_from_netcdf(l1b_path_c02, "Rad");
    DataF rad_c03 = dataf_load_from_netcdf(l1b_path_c03, "Rad");

    // 3. CARGA DE GEOMETRÍA ESTÁTICA (NAVF)
    LOG_INFO("Paso 2: Cargando Geometría Estática (NAVF)...");
    DataF navla = dataf_load_from_netcdf(nav_path, "Latitude");
    DataF navlo = dataf_load_from_netcdf(nav_path, "Longitude");
    DataF nav_vza = dataf_load_from_netcdf(nav_path, "SenZenAng");
    DataF nav_vaa = dataf_load_from_netcdf(nav_path, "SenAziAng");
    if (navla.data_in == NULL || navlo.data_in == NULL || nav_vza.data_in == NULL || nav_vaa.data_in == NULL) {
        LOG_FATAL("Falla al cargar archivos NAVF (Geometría). Terminando.");
        return 1;
    }
    
    // !! IMPORTANTE: Verificación de Dimensiones !!
    // Asumimos que L1b (10848) y NAVF (5424) no coinciden.
    // Aquí iría tu lógica de remuestreo (downsample) de L1b para que coincida con NAVF (5424x5424)
    // O (mejor) remuestrear NAVF (5424) para que coincida con L1b (10848)
    // Por simplicidad, este código asume que YA COINCIDEN (p.ej. 5424x5424)
    if (c01_nc.base.width != navla.width || c01_nc.base.height != navla.height) {
        LOG_WARN("Las dimensiones de L1b y NAVF no coinciden. Se requiere remuestreo (no implementado aquí).");
        // ... (Tu lógica de remuestreo iría aquí) ...
    }

    // 4. CARGA DE TABLAS DE BÚSQUEDA (LUTs)
    LOG_INFO("Paso 3: Cargando LUTs de Corrección...");
    RayleighLUT lut_c01 = lut_load_for_band(1, lut_path_sunglint, lut_path_aerosol);
    RayleighLUT lut_c02 = lut_load_for_band(2, lut_path_sunglint, lut_path_aerosol);
    RayleighLUT lut_c03 = lut_load_for_band(3, lut_path_sunglint, lut_path_aerosol);
    if (lut_c01.data == NULL || lut_c02.data == NULL || lut_c03.data == NULL) {
        LOG_FATAL("Falla al cargar una o más LUTs. Terminando.");
        return 1;
    }

    // 5. CÁLCULO DE GEOMETRÍA DINÁMICA (Solares)
    LOG_INFO("Paso 4: Calculando Geometría Solar (SZA/SAA)...");
    DataF sza_map = dataf_create(navla.width, navla.height);
    DataF saa_map = dataf_create(navla.width, navla.height);
    create_solar_maps(&c01_nc, &navla, &navlo, &sza_map, &saa_map);

    // 6. CÁLCULO DE GEOMETRÍA RELATIVA (RA)
    LOG_INFO("Paso 5: Calculando Ángulo Acimutal Relativo (RA)...");
    DataF ra_map = dataf_create(navla.width, navla.height);
    create_relative_azimuth_map(&nav_vaa, &saa_map, &ra_map);

    // 7. APLICACIÓN DE LA CORRECCIÓN DE RAYLEIGH
    LOG_INFO("Paso 6: Aplicando Corrección de Rayleigh...");
    DataF rho_c01 = dataf_create(c01_nc.base.width, c01_nc.base.height);
    DataF rho_c02 = dataf_create(c01_nc.base.width, c01_nc.base.height);
    DataF rho_c03 = dataf_create(c01_nc.base.width, c01_nc.base.height);
    
    apply_rayleigh_correction(&c01_nc.base, &sza_map, &nav_vza, &ra_map, &lut_c01, &rho_c01);
    apply_rayleigh_correction(&rad_c02,     &sza_map, &nav_vza, &ra_map, &lut_c02, &rho_c02);
    apply_rayleigh_correction(&rad_c03,     &sza_map, &nav_vza, &ra_map, &lut_c03, &rho_c03);

    // 8. CREACIÓN DE IMAGEN TRUE COLOR RGB
    LOG_INFO("Paso 7: Creando imagen RGB final...");
    ImageData final_rgb_image = create_truecolor_rgb(rho_c01, rho_c02, rho_c03);
    
    // 9. GUARDAR RESULTADO
    // (Descomenta esto cuando tengas tu 'writer_png.h' listo)
    // if (final_rgb_image.data) {
    //     LOG_INFO("Paso 8: Guardando imagen en 'goes_truecolor_corrected.png'...");
    //     image_save_png(&final_rgb_image, "goes_truecolor_corrected.png");
    // } else {
    //     LOG_ERROR("Falla al crear la imagen RGB final.");
    // }

    // 10. LIMPIEZA DE MEMORIA
    LOG_INFO("Paso 9: Liberando memoria...");
    dataf_destroy(&c01_nc.base); dataf_destroy(&rad_c02); dataf_destroy(&rad_c03);
    dataf_destroy(&navla); dataf_destroy(&navlo); dataf_destroy(&nav_vza); dataf_destroy(&nav_vaa);
    dataf_destroy(&sza_map); dataf_destroy(&saa_map); dataf_destroy(&ra_map);
    dataf_destroy(&rho_c01); dataf_destroy(&rho_c02); dataf_destroy(&rho_c03);
    lut_destroy(&lut_c01); lut_destroy(&lut_c02); lut_destroy(&lut_c03);
    image_destroy(&final_rgb_image);

    LOG_INFO("Proceso completado.");
    return 0;
}


// ====================================================================
// IMPLEMENTACIÓN DE FUNCIONES AUXILIARES
// ====================================================================

// ---
// 1. LÓGICA DE GEOMETRÍA SOLAR (Tu implementación de daynight_mask.c)
// ---
//
void sun_geometry(float la, float lo, DataNC datanc, 
                  double *angle, double *zenith_out, double *azimuth_out) {
  
  // input data:
  double UT;
  int Day;
  int Month;
  int Year;
  double Longitude;
  double Latitude;

  // output data
  double RightAscension;
  double Declination;
  double HourAngle;
  double Zenith;
  double Azimuth; // Añadido

  // auxiliary
  double t, te, wte, s1, c1, s2, c2, c3, sp, cp, sd, cd, cH, sH, // sH añadido
      se0, ep, De, epsi, sl, cl, se, ce, L, nu, Dlam;
  int yt, mt;

  double PI = M_PI;
  double PI2 = 2 * M_PI;

  UT = datanc.hour + datanc.min / 60.0 + datanc.sec / 3600.0;
  Day = datanc.day;
  Month = datanc.mon;
  Year = datanc.year;
  
  Latitude = la * PI / 180.0; // Convertir a radianes
  Longitude = lo * PI / 180.0; // Convertir a radianes

  if (Month <= 2) {
    yt = Year - 1;
    mt = Month + 12;
  } else {
    yt = Year;
    mt = Month;
  }
  t = (int)(365.25 * yt) + (int)(30.6001 * (mt + 1)) + Day + UT / 24.0 +
      1720981.5;
  t = (t - 2451545.0) / 36525.0;
  te = t + (0.1712e-2) * sin(PI2 * (0.0048 * Year + 0.0411 * Month +
                                   0.0018 * Day - 0.162)) -
       (0.1299e-2) * sin(PI2 * (0.013 * Year + 0.31 * Month + 0.012 * Day));
  wte = 0.000004 * (Year - 1950);
  t = te + wte;

  s1 = sin(PI2 * (0.993126 + 29.1053527 * t));
  c1 = cos(PI2 * (0.993126 + 29.1053527 * t));
  s2 = sin(PI2 * (0.6919 + 381.997 * t));
  c2 = cos(PI2 * (0.6919 + 381.997 * t));
  //s3 = sin(PI2 * (0.087 + 4.44 * t));
  c3 = cos(PI2 * (0.087 + 4.44 * t));

  L = 4.882594 + 628.33195 * t + (0.000033 * c1 - 0.000028 * s1) +
      (0.000305 * c2 - 0.000349 * s2) + (0.000005 * c3);
  ep = 0.409105 - 0.000227 * t;
  De = 0.0025 - 0.0048 * cos(PI2 * (0.012 * Year + 0.3 * Month + 0.012 * Day)) +
       0.007 * cos(PI2 * (0.005 * Year + 0.04 * Month + 0.002 * Day));
  epsi = ep + De;
  sl = sin(L);
  cl = cos(L);
  se = sin(epsi);
  ce = cos(epsi);
  Declination = atan2(sl * se, sqrt(cl * cl + sl * sl * ce * ce));
  RightAscension = atan2(sl * ce, cl);
  nu = RightAscension;

  if (nu < 0) {
    nu += PI2;
  }
  if (L < 0) {
    L += PI2;
  }
  Dlam = L - nu;
  if (Dlam > PI) {
    Dlam -= PI2;
  }
  if (Dlam < -PI) {
    Dlam += PI2;
  }

  HourAngle =
      1.7528311 + 6.300388099 * t + Longitude - RightAscension + 0.92 * Dlam;
  HourAngle = fmod(HourAngle + PI, PI2) - PI;
  if (HourAngle < -PI)
    HourAngle += PI2;

  sp = sin(Latitude);
  cp = cos(Latitude); // Corrección: era sqrt((1 - sp * sp))
  sd = sin(Declination);
  cd = cos(Declination); // Corrección: era sqrt(1 - sd * sd)
  sH = sin(HourAngle);   // Añadido
  cH = cos(HourAngle);
  se0 = sp * sd + cp * cd * cH;
  ep = asin(se0) - 4.26e-5 * sqrt(1.0 - se0 * se0);
  Zenith = M_PI_2 - ep;

  // Cálculo de Azimut (descomentado y asignado)
  Azimuth = atan2(sH, cH * sp - sd * cp / cd);

  // Asignar a los punteros de salida (en radianes)
  if (zenith_out) *zenith_out = Zenith;
  if (azimuth_out) *azimuth_out = Azimuth;
}


void create_solar_maps(const DataNC *l1b_data, const DataF *navla, const DataF *navlo, 
                       DataF *sza_map_out, DataF *saa_map_out) {
    size_t size = navla->size;
    LOG_INFO("Calculando mapas SZA/SAA para %zu píxeles...", size);
    double start = omp_get_wtime();
    
#pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
        float la = navla->data_in[i];
        float lo = navlo->data_in[i];
        if (la == NonData || lo == NonData) {
            sza_map_out->data_in[i] = NonData;
            saa_map_out->data_in[i] = NonData;
            continue;
        }
        double sza_rad, saa_rad;
        sun_geometry(la, lo, *l1b_data, NULL, &sza_rad, &saa_rad);
        
        sza_map_out->data_in[i] = (float)(sza_rad * 180.0 / M_PI); // Convertir a grados
        saa_map_out->data_in[i] = (float)(saa_rad * 180.0 / M_PI); // Convertir a grados
    }
    LOG_INFO("Cálculo solar completado en %.2f s", omp_get_wtime() - start);
}

int create_relative_azimuth_map(const DataF *vaa_in, const DataF *saa_in, DataF *ra_map_out) {
    size_t size = vaa_in->size;
#pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
        float solar_az_deg = saa_in->data_in[i];
        float sensor_az_deg = vaa_in->data_in[i];
        if (solar_az_deg == NonData || sensor_az_deg == NonData) {
            ra_map_out->data_in[i] = NonData;
            continue;
        }
        float ra_deg = fabs(solar_az_deg - sensor_az_deg);
        if (ra_deg > 180.0f) ra_deg = 360.0f - ra_deg;
        ra_map_out->data_in[i] = ra_deg;
    }
    return 1;
}


// ---
// 2. LÓGICA DE CARGA DE LUT (CSPP GEO)
// ---
#define ERRCODE 2
#define ERR(e) { LOG_ERROR("Error NetCDF: %s", nc_strerror(e)); return 0; }

static int nc_load_lut_array_1d(int ncid, const char *varname, float **data_out, size_t *size_out) {
    int retval, varid, dimid, ndims;
    if ((retval = nc_inq_varid(ncid, varname, &varid))) ERR(retval);
    if ((retval = nc_inq_varndims(ncid, varid, &ndims))) ERR(retval);
    if (ndims != 1) { LOG_ERROR("%s no es 1D", varname); return 0; }
    if ((retval = nc_inq_vardimid(ncid, varid, &dimid))) ERR(retval);
    if ((retval = nc_inq_dimlen(ncid, dimid, size_out))) ERR(retval);
    *data_out = (float*)malloc(*size_out * sizeof(float));
    if (!*data_out) { LOG_ERROR("Falla de memoria (1D)"); return 0; }
    if ((retval = nc_get_var_float(ncid, varid, *data_out))) { free(*data_out); *data_out = NULL; ERR(retval); }
    return 1;
}

static int nc_load_lut_slice_3d(int ncid, const char *varname, int band_idx, 
                                size_t sza_dim, size_t vza_dim, size_t ra_dim, 
                                float **data_out) {
    int retval, varid, ndims;
    size_t start[NC_MAX_VAR_DIMS] = {0}, count[NC_MAX_VAR_DIMS] = {0};
    size_t expected_size = sza_dim * vza_dim * ra_dim;
    if ((retval = nc_inq_varid(ncid, varname, &varid))) ERR(retval);
    if ((retval = nc_inq_varndims(ncid, varid, &ndims))) ERR(retval);
    *data_out = (float *)malloc(expected_size * sizeof(float));
    if (!*data_out) { LOG_ERROR("Falla de memoria (slice 3D)"); return 0; }
    size_t channel_index = (size_t)band_idx - 1;

    if (ndims == 5) { // Asume [NChn, NAer, SZA, VZA, RA]
        start[0] = channel_index; count[0] = 1;
        start[1] = 0;             count[1] = 1;
        start[2] = 0;             count[2] = sza_dim;
        start[3] = 0;             count[3] = vza_dim;
        start[4] = 0;             count[4] = ra_dim;
    } else if (ndims == 4) { // Asume [NChn, SZA, VZA, RA]
        start[0] = channel_index; count[0] = 1;
        start[1] = 0;             count[1] = sza_dim;
        start[2] = 0;             count[2] = vza_dim;
        start[3] = 0;             count[3] = ra_dim;
    } else {
        LOG_ERROR("%s tiene dims inesperadas (%d)", varname, ndims);
        free(*data_out); return 0;
    }
    if ((retval = nc_get_vara_float(ncid, varid, start, count, *data_out))) {
        free(*data_out); *data_out = NULL; ERR(retval);
    }
    return 1;
}

static int nc_load_and_expand_2d(int ncid, const char *varname, int band_idx, 
                                 size_t sza_dim, size_t vza_dim, size_t ra_dim, 
                                 float **data_out) {
    int retval, varid;
    size_t start[2] = {0}, count[2] = {0};
    size_t component_size_3d = sza_dim * vza_dim * ra_dim;
    float *sza_slice_1d = (float*)malloc(sza_dim * sizeof(float));
    if (!sza_slice_1d) { LOG_ERROR("Falla de memoria (expand 2D buf)"); return 0; }
    if ((retval = nc_inq_varid(ncid, varname, &varid))) { free(sza_slice_1d); ERR(retval); }
    start[0] = (size_t)band_idx - 1; count[0] = 1;
    start[1] = 0;                    count[1] = sza_dim;
    if ((retval = nc_get_vara_float(ncid, varid, start, count, sza_slice_1d))) {
        free(sza_slice_1d); ERR(retval);
    }
    *data_out = (float *)malloc(component_size_3d * sizeof(float));
    if (!*data_out) { free(sza_slice_1d); LOG_ERROR("Falla de memoria (expand 2D)"); return 0; }
    
    #pragma omp parallel for
    for (size_t i = 0; i < sza_dim; i++) {
        float val = sza_slice_1d[i];
        for (size_t j = 0; j < vza_dim; j++) {
            for (size_t k = 0; k < ra_dim; k++) {
                (*data_out)[i * (vza_dim * ra_dim) + j * ra_dim + k] = val;
            }
        }
    }
    free(sza_slice_1d);
    return 1;
}

static int nc_load_and_expand_1d(int ncid, const char *varname, int band_idx, 
                                 size_t component_size_3d, float **data_out) {
    int retval, varid;
    size_t start[1] = {0}, count[1] = {1};
    float value_1d;
    if ((retval = nc_inq_varid(ncid, varname, &varid))) ERR(retval);
    start[0] = (size_t)band_idx - 1;
    if ((retval = nc_get_vara_float(ncid, varid, start, count, &value_1d))) ERR(retval);
    *data_out = (float *)malloc(component_size_3d * sizeof(float));
    if (!*data_out) { LOG_ERROR("Falla de memoria (expand 1D)"); return 0; }
    
    #pragma omp parallel for
    for (size_t i = 0; i < component_size_3d; i++) (*data_out)[i] = value_1d;
    return 1;
}

RayleighLUT lut_load_for_band(int band_id, const char *knots_path, const char *data_path) {
    RayleighLUT lut;
    memset(&lut, 0, sizeof(RayleighLUT)); 
    int ncid_knots = 0, ncid_data = 0;
    float *rtc_buffers[NUM_COMPONENTS] = {NULL};
    const char *knot_vars[3] = {"solar_zenith_angle", "sensor_zenith_angle", "relative_azimuth_angle"};
    const char *rtc_vars[NUM_COMPONENTS] = {"ray_path_refl", "ray_trans", "ray_trans", "ray_sph_alb"};
    
    if (nc_open(knots_path, NC_NOWRITE, &ncid_knots)) { LOG_ERROR("Falla al abrir LUT knots: %s", knots_path); goto cleanup; }
    if (!nc_load_lut_array_1d(ncid_knots, knot_vars[0], &lut.sza_knots, &lut.sza_dim)) goto cleanup;
    if (!nc_load_lut_array_1d(ncid_knots, knot_vars[1], &lut.vza_knots, &lut.vza_dim)) goto cleanup;
    if (!nc_load_lut_array_1d(ncid_knots, knot_vars[2], &lut.ra_knots, &lut.ra_dim)) goto cleanup;
    nc_close(ncid_knots); ncid_knots = 0;
    LOG_INFO("LUT Knots cargados (SZA:%zu, VZA:%zu, RA:%zu)", lut.sza_dim, lut.vza_dim, lut.ra_dim);
    
    lut.component_size = lut.sza_dim * lut.vza_dim * lut.ra_dim;
    if (lut.component_size == 0) goto cleanup;

    if (nc_open(data_path, NC_NOWRITE, &ncid_data)) { LOG_ERROR("Falla al abrir LUT data: %s", data_path); goto cleanup; }
    if (!nc_load_lut_slice_3d(ncid_data, rtc_vars[0], band_id, lut.sza_dim, lut.vza_dim, lut.ra_dim, &rtc_buffers[0])) goto cleanup;
    if (!nc_load_and_expand_2d(ncid_data, rtc_vars[1], band_id, lut.sza_dim, lut.vza_dim, lut.ra_dim, &rtc_buffers[1])) goto cleanup;
    if (!nc_load_and_expand_2d(ncid_data, rtc_vars[2], band_id, lut.sza_dim, lut.vza_dim, lut.ra_dim, &rtc_buffers[2])) goto cleanup;
    if (!nc_load_and_expand_1d(ncid_data, rtc_vars[3], band_id, lut.component_size, &rtc_buffers[3])) goto cleanup;
    nc_close(ncid_data); ncid_data = 0;

    lut.data = (float *)malloc(lut.component_size * NUM_COMPONENTS * sizeof(float));
    if (!lut.data) { LOG_ERROR("Falla de memoria (LUT data final)"); goto cleanup; }
    
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        memcpy(lut.data + (i * lut.component_size), rtc_buffers[i], lut.component_size * sizeof(float));
        free(rtc_buffers[i]); rtc_buffers[i] = NULL;
    }
    LOG_INFO("LUT para banda %d cargada exitosamente.", band_id);
    return lut;

cleanup:
    LOG_FATAL("FALLO al cargar la LUT para banda %d.", band_id);
    if (ncid_knots > 0) nc_close(ncid_knots);
    if (ncid_data > 0) nc_close(ncid_data);
    for (int i = 0; i < NUM_COMPONENTS; i++) if (rtc_buffers[i]) free(rtc_buffers[i]);
    lut_destroy(&lut); 
    return (RayleighLUT){0};
}

void lut_destroy(RayleighLUT *lut) {
    if (lut) {
        if (lut->data) free(lut->data);
        if (lut->sza_knots) free(lut->sza_knots);
        if (lut->vza_knots) free(lut->vza_knots);
        if (lut->ra_knots) free(lut->ra_knots);
        memset(lut, 0, sizeof(RayleighLUT)); 
    }
}

// ---
// 3. LÓGICA DE CORRECCIÓN (INTERPOLACIÓN)
// ---
static inline float lerp(float v0, float v1, float w) {
    return v0 + w * (v1 - v0);
}

// Encuentra los índices y pesos para la interpolación 1D
static void find_indices_and_weights(float val, const float *knots, size_t num_dims,
                                     size_t *idx0, size_t *idx1, float *weight1) {
    // Búsqueda simple (asume nudos monótonos crecientes)
    size_t i = 0;
    while (i < num_dims - 1 && knots[i+1] < val) i++;
    
    *idx0 = i;
    *idx1 = i + 1;

    // Manejo de bordes (extrapolación constante)
    if (val < knots[0]) { 
        *idx0 = 0; *idx1 = 0; *weight1 = 0.0f; return; 
    }
    if (val >= knots[num_dims - 1]) { 
        *idx0 = num_dims - 1; *idx1 = num_dims - 1; *weight1 = 0.0f; return; 
    }

    float val0 = knots[*idx0], val1 = knots[*idx1];
    float denom = val1 - val0;
    *weight1 = (denom > 1e-6) ? ((val - val0) / denom) : 0.0f;
}

// Interpola un valor de la LUT 3D
static float interpolate_trilinear(const RayleighLUT *lut, float sza_val, float vza_val, 
                                   float ra_val, LutComponent component) {
    size_t i0, i1, j0, j1, k0, k1;
    float sza_w, vza_w, ra_w;

    find_indices_and_weights(sza_val, lut->sza_knots, lut->sza_dim, &i0, &i1, &sza_w);
    find_indices_and_weights(vza_val, lut->vza_knots, lut->vza_dim, &j0, &j1, &vza_w);
    find_indices_and_weights(ra_val, lut->ra_knots, lut->ra_dim, &k0, &k1, &ra_w);

    // Puntero al inicio del componente específico
    const float *data = lut->data + (component * lut->component_size);
    size_t vza_stride = lut->vza_dim * lut->ra_dim;
    size_t ra_stride = lut->ra_dim;

    // Obtener los 8 puntos del cubo para interpolar
    float c000 = data[i0*vza_stride + j0*ra_stride + k0];
    float c001 = data[i0*vza_stride + j0*ra_stride + k1];
    float c010 = data[i0*vza_stride + j1*ra_stride + k0];
    float c011 = data[i0*vza_stride + j1*ra_stride + k1];
    float c100 = data[i1*vza_stride + j0*ra_stride + k0];
    float c101 = data[i1*vza_stride + j0*ra_stride + k1];
    float c110 = data[i1*vza_stride + j1*ra_stride + k0];
    float c111 = data[i1*vza_stride + j1*ra_stride + k1];

    // Interpolar en RA (eje k)
    float c00 = lerp(c000, c001, ra_w);
    float c01 = lerp(c010, c011, ra_w);
    float c10 = lerp(c100, c101, ra_w);
    float c11 = lerp(c110, c111, ra_w);
    // Interpolar en VZA (eje j)
    float c0 = lerp(c00, c01, vza_w);
    float c1 = lerp(c10, c11, vza_w);
    // Interpolar en SZA (eje i)
    return lerp(c0, c1, sza_w);
}

void apply_rayleigh_correction(const DataF *L_TOA, const DataF *sza_map, 
                               const DataF *vza_map, const DataF *ra_map, 
                               const RayleighLUT *lut, DataF *rho_surface_out) {
    size_t size = L_TOA->size;
    const float PI = 3.1415926535f;
#pragma omp parallel for
    for (size_t i = 0; i < size; ++i) {
        float L_TOA_i = L_TOA->data_in[i], SZA_i = sza_map->data_in[i],
              VZA_i = vza_map->data_in[i], RA_i = ra_map->data_in[i];
        
        if (L_TOA_i == NonData || SZA_i == NonData || VZA_i == NonData || RA_i == NonData) {
            rho_surface_out->data_in[i] = NonData; continue;
        }
        // Noche (o ángulo solar muy alto)
        if (SZA_i > 89.0f) { 
            rho_surface_out->data_in[i] = 0.0f; continue; 
        }

        // 1. Interpolar los 4 componentes RTC
        float Lp_0 = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, LP_0_COMP);
        float T_up = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, T_UP_COMP);
        float E_down = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, E_DOWN_COMP);
        float S_alb = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, S_ALB_COMP);

        // 2. Aplicar Ecuación (Eq. 1 del artículo de Broomhall)
        float denominator_A = E_down * T_up;
        if (fabs(denominator_A) < 1e-9) { rho_surface_out->data_in[i] = 0.0f; continue; }
        
        float numerator_A = PI * (L_TOA_i - Lp_0);
        float A = numerator_A / denominator_A;
        
        float denominator_rho = 1.0f + A * S_alb;
        float rho_surface_i = (fabs(denominator_rho) < 1e-9) ? 0.0f : (A / denominator_rho);
        
        // 3. Restringir (Clamp) reflectancia a un rango válido [0, 1]
        if (rho_surface_i < 0.0f) rho_surface_i = 0.0f;
        if (rho_surface_i > 1.0f) rho_surface_i = 1.0f;
        rho_surface_out->data_in[i] = rho_surface_i;
    }
}

// ---
// 4. LÓGICA DE CREACIÓN DE RGB (De truecolor_rgb.c)
// ---
static inline unsigned char apply_gamma(float reflectance, float gamma) {
    float corrected = powf(reflectance, 1.0f / gamma);
    if (corrected > 1.0f) corrected = 1.0f;
    return (unsigned char)(corrected * 255.0f);
}

ImageData create_truecolor_rgb(DataF rho_c01, DataF rho_c02, DataF rho_c03) {
    ImageData imout = image_create(rho_c01.width, rho_c01.height, 3);
    if (imout.data == NULL) {
        LOG_ERROR("Falla de memoria al crear imagen RGB");
        return imout; 
    }
    const float GAMMA = 1.8f; // Gamma para hacerlo más vívido
#pragma omp parallel for
    for (int i = 0; i < imout.width * imout.height; i++) {
        int po = i * imout.bpp;
        float c01f = rho_c01.data_in[i]; // Azul (Banda 1)
        float c02f = rho_c02.data_in[i]; // Rojo (Banda 2)
        float c03f = rho_c03.data_in[i]; // Veggie (Banda 3)

        if (c01f == NonData || c02f == NonData || c03f == NonData) {
            imout.data[po] = 0; imout.data[po + 1] = 0; imout.data[po + 2] = 0;
            continue;
        }
        
        // Fórmula de Verde Sintético para GOES (de tu truecolor_rgb.c)
        float gg = 0.48358168 * c02f + 0.45706946 * c01f + 0.08038137 * c03f;
        if (gg < 0.0f) 
            gg = 0.0f; 
        if (gg > 1.0f) 
            gg = 1.0f;

        // Aplicar Corrección Gamma y asignar
        imout.data[po] = apply_gamma(c02f, GAMMA);     // R (Banda 2)
        imout.data[po + 1] = apply_gamma(gg, GAMMA);   // G (Sintético)
        imout.data[po + 2] = apply_gamma(c01f, GAMMA); // B (Banda 1)
    }
    return imout;
}

