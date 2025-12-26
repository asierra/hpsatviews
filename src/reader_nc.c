/* NetCDF Data reader
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "logger.h"
#include <math.h>
#include <netcdf.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERRCODE 2
#define ERR(e)                                                                 \
  {                                                                            \
    LOG_ERROR("NetCDF error: %s", nc_strerror(e));                             \
    return (ERRCODE);                                                          \
  }
#define WRN(e)                                                                 \
  {                                                                            \
    LOG_WARN("NetCDF warning: %s", nc_strerror(e));                            \
  }

// Carga un conjunto de datos de NetCDF y lo pone en una estructura DataNC
// Específico para datos L1b de GOES
int load_nc_sf(const char *filename, const char *variable, DataNC *datanc) {
  int retval;
  int ncid;
  bool is_l1b = (strcmp(variable, "Rad") == 0);
  
  if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
    ERR(retval);

  int xid;
  if ((retval = nc_inq_dimid(ncid, "x", &xid)))
    ERR(retval);
  int yid;
  if ((retval = nc_inq_dimid(ncid, "y", &yid)))
    ERR(retval);

  // Recuperamos las dimensiones de los datos
  size_t width, height, total_size;
  if ((retval = nc_inq_dimlen(ncid, xid, &width)))
    ERR(retval);
  if ((retval = nc_inq_dimlen(ncid, yid, &height)))
    ERR(retval);
  total_size = width * height;
  LOG_INFO("NetCDF dimensions: %lux%lu (total: %lu)", width, height,
           total_size);

  // Leer resolución espacial nativa del sensor (atributo global)
  char spatial_res_str[128];
  datanc->native_resolution_km = 0.0f; // Por defecto
  if ((retval = nc_get_att_text(ncid, NC_GLOBAL, "spatial_resolution",
                                spatial_res_str)) == NC_NOERR) {
    spatial_res_str[127] = '\0'; // Asegurar terminación
    // El atributo típicamente es algo como "1km at nadir" o "2km at nadir"
    float res_val;
    if (sscanf(spatial_res_str, "%fkm", &res_val) == 1) {
      datanc->native_resolution_km = res_val;
      LOG_INFO("Native sensor resolution: %.1f km",
               datanc->native_resolution_km);
    }
  }

  // Obtenemos el id de la variable
  int rad_varid;
  if ((retval = nc_inq_varid(ncid, variable, &rad_varid)))
    ERR(retval);

  // Obtiene el factor de escala y offset de la variable y el fillvalue
  float scale_factor, add_offset;
  if ((retval =
           nc_get_att_float(ncid, rad_varid, "scale_factor", &scale_factor)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, rad_varid, "add_offset", &add_offset)))
    WRN(retval);
  short fillvalue;
  if ((retval = nc_get_att_short(ncid, rad_varid, "_FillValue", &fillvalue)))
    WRN(retval);
  LOG_INFO("NetCDF scaling: factor=%g, offset=%g, fill_value=%d", scale_factor,
           add_offset, fillvalue);

  // Recupera los datos

  // Primero verificar el tipo de datos
  nc_type var_type;
  if ((retval = nc_inq_vartype(ncid, rad_varid, &var_type)))
    ERR(retval);

  if (var_type!=NC_BYTE && var_type!=NC_UBYTE && var_type!=NC_SHORT && var_type!=NC_USHORT) {
    LOG_FATAL("Unsupported data type %d", var_type);
	  return -1;
  }
    
  size_t type_size =
      (var_type == NC_SHORT) ? sizeof(short) : sizeof(char);
  void *datatmp = malloc(type_size * total_size);
  if (datatmp == NULL) {
    LOG_FATAL("Failed to allocate memory for NetCDF data");
    return ERRCODE;
  }
  if ((retval = nc_get_var(ncid, rad_varid, datatmp))) // Lectura genérica
    ERR(retval);

  // Obtenemos el instante
  int time_varid;
  if ((retval = nc_inq_varid(ncid, "t", &time_varid)))
    ERR(retval);
  double tiempo;
  if ((retval = nc_get_var_double(ncid, time_varid, &tiempo)))
    ERR(retval);
  long tt = (long)(tiempo);
  time_t dt = 946728000 + tt;
  struct tm ts = *gmtime(&dt);
  datanc->year = ts.tm_year + 1900;
  datanc->mon = ts.tm_mon + 1;
  datanc->day = ts.tm_mday;
  datanc->hour = ts.tm_hour;
  datanc->min = ts.tm_min;
  datanc->sec = ts.tm_sec;

  // Identificamos la banda, si existe
  int band_varid;
  if (nc_inq_varid(ncid, "band_id", &band_varid) == NC_NOERR) {
    int bid_val;
    size_t index = 0;
    // Leemos el primer (y usualmente único) elemento de la variable
    if (nc_get_var1_int(ncid, band_varid, &index, &bid_val) == NC_NOERR) {
      datanc->band_id = (uint8_t)bid_val;
      LOG_DEBUG("ID de banda detectado en metadatos: C%02d", datanc->band_id);
    }
  } else {
    LOG_WARN("No se encontró la variable 'band_id' en el archivo NetCDF.");
  }
  
  // Only L1b
  float planck_fk1, planck_fk2, planck_bc1, planck_bc2, kappa0;
  if (is_l1b) {
    // Obtiene los parámetros de Planck o Kappa0
    if (datanc->band_id > 6) {
      int fk1_varid, fk2_varid, bc1_varid, bc2_varid;
      if ((retval = nc_inq_varid(ncid, "planck_fk1", &fk1_varid)))
        ERR(retval);
      if ((retval = nc_get_var_float(ncid, fk1_varid, &planck_fk1)))
        ERR(retval);
      if ((retval = nc_inq_varid(ncid, "planck_fk2", &fk2_varid)))
        ERR(retval);
      if ((retval = nc_get_var_float(ncid, fk2_varid, &planck_fk2)))
        ERR(retval);
      if ((retval = nc_inq_varid(ncid, "planck_bc1", &bc1_varid)))
        ERR(retval);
      if ((retval = nc_get_var_float(ncid, bc1_varid, &planck_bc1)))
        ERR(retval);
      if ((retval = nc_inq_varid(ncid, "planck_bc2", &bc2_varid)))
        ERR(retval);
      if ((retval = nc_get_var_float(ncid, bc2_varid, &planck_bc2)))
        ERR(retval);
    } else if (datanc->band_id >= 1) {
      int kappa0_varid;
      if ((retval = nc_inq_varid(ncid, "kappa0", &kappa0_varid)))
        ERR(retval);
      if ((retval = nc_get_var_float(ncid, kappa0_varid, &kappa0)))
        ERR(retval);
    }
  }

  // =========================================================================
  // LECTURA DE METADATOS DE PROYECCIÓN Y GEOTRANSFORM (Para GeoTIFF)
  // =========================================================================
  datanc->proj_code = PROJ_UNKNOWN;
  datanc->proj_info.valid = false;
  int proj_varid;

  // 1. Leer parámetros de la proyección (Elipsoide y Altura)
  if (nc_inq_varid(ncid, "goes_imager_projection", &proj_varid) == NC_NOERR) {
    datanc->proj_code = PROJ_GEOS;
    nc_get_att_double(ncid, proj_varid, "perspective_point_height",
                      &datanc->proj_info.sat_height);
    nc_get_att_double(ncid, proj_varid, "semi_major_axis",
                      &datanc->proj_info.semi_major);
    nc_get_att_double(ncid, proj_varid, "semi_minor_axis",
                      &datanc->proj_info.semi_minor);
    nc_get_att_double(ncid, proj_varid, "longitude_of_projection_origin",
                      &datanc->proj_info.lon_origin);
    nc_get_att_double(ncid, proj_varid, "inverse_flattening",
                      &datanc->proj_info.inv_flat);
    datanc->proj_info.valid = true;
  }

  // 2. Calcular GeoTransform (Bounding Box y Resolución)
  // Necesitamos leer el primer valor de 'x' y 'y' y sus factores de escala
  int x_varid_coord, y_varid_coord;
  if (datanc->proj_info.valid &&
      nc_inq_varid(ncid, "x", &x_varid_coord) == NC_NOERR &&
      nc_inq_varid(ncid, "y", &y_varid_coord) == NC_NOERR) {

    // Aunque el NetCDF define scale_factor como float, al multiplicarlo
    // por la altura del satélite (~35 millones de metros), la falta de
    // precisión del float introduce un error de desplazamiento de ~200m
    double x_scale, x_offset, y_scale, y_offset;
    nc_get_att_double(ncid, x_varid_coord, "scale_factor", &x_scale);
    nc_get_att_double(ncid, x_varid_coord, "add_offset", &x_offset);
    nc_get_att_double(ncid, y_varid_coord, "scale_factor", &y_scale);
    nc_get_att_double(ncid, y_varid_coord, "add_offset", &y_offset);

    // Leer el valor del primer píxel (índice 0) de X e Y
    // NetCDF GOES almacena las coordenadas en variables 1D 'x' y 'y'
    short x0_raw, y0_raw;
    size_t index[] = {0};
    nc_get_var1_short(ncid, x_varid_coord, index, &x0_raw);
    nc_get_var1_short(ncid, y_varid_coord, index, &y0_raw);

    // Convertir a radianes (unidades de proyección)
    double x0_rad = (double)x0_raw * x_scale + x_offset;
    double y0_rad = (double)y0_raw * y_scale + y_offset;

    // --- Construcción del GeoTransform ---
    // GDAL requiere la esquina SUPERIOR IZQUIERDA del píxel, no el centro.
    // Por eso restamos (o sumamos) medio píxel.

    // GT[0]: Top Left X
    datanc->geotransform[0] = x0_rad - (x_scale / 2.0);

    // GT[1]: Ancho de píxel (siempre positivo en proyección fija)
    datanc->geotransform[1] = x_scale;

    // GT[2]: Rotación X (0)
    datanc->geotransform[2] = 0.0;

    // GT[3]: Top Left Y
    // Nota: En GOES 'y_scale' suele ser negativo (-2.8e-05)
    // Si y0_rad es el centro del primer píxel (Norte), la esquina está "arriba"
    // (hacia valores más positivos si es radianes N>S, pero cuidado con el
    // signo). Matemáticamente: Centro + (Tamaño / 2) * (-SignoEscala) ...
    // simplificado:
    datanc->geotransform[3] = y0_rad - (y_scale / 2.0);

    // GT[4]: Rotación Y (0)
    datanc->geotransform[4] = 0.0;

    // GT[5]: Alto de píxel (debe ser negativo para imágenes N->S standard)
    datanc->geotransform[5] = y_scale;

    LOG_INFO("GeoTransform calculado: Origin (%.6f, %.6f) Res (%.6f, %.6f)",
             datanc->geotransform[0], datanc->geotransform[3],
             datanc->geotransform[1], datanc->geotransform[5]);
  }
  // =========================================================================

  if ((retval = nc_close(ncid)))
    ERR(retval);

  // Aplica factor de escala, offset y parámetros
  // Calcula máximos y mínimos para verificar datos
  float fmin = 1e20;
  float fmax = -fmin;
  unsigned nondatas = 0;

  if (var_type != NC_SHORT) {
    // --- RUTA PARA DATOS TIPO BYTE (ej. Cloud Phase) ---
    LOG_DEBUG("Leyendo tipo de datos BYTE");
    datanc->is_float = false;
    datanc->bdata = datab_create(width, height);
    if (datanc->bdata.data_in == NULL) {
      LOG_FATAL("Memory allocation failed for byte data buffer");
      free(datatmp);
      return ERRCODE;
    }
    signed char *src_buffer = (signed char *)datatmp;

    for (size_t i = 0; i < total_size; i++) {
      if (src_buffer[i] != (signed char)fillvalue) {
        datanc->bdata.data_in[i] = (int8_t)src_buffer[i];
      } else {
        // Usar -128 como valor NonData para int8_t
        datanc->bdata.data_in[i] = -128;
        nondatas++;
      }
    }
  } else {
    // --- RUTA PARA DATOS TIPO SHORT (ej. Radiancia) ---   
    LOG_DEBUG("Leyendo tipo de datos SHORT"); 
    datanc->is_float = true;
    datanc->fdata = dataf_create(width, height);
    if (datanc->fdata.data_in == NULL) {
      LOG_FATAL("Memory allocation failed for float data buffer");
      free(datatmp);
      return ERRCODE;
    }
    short *src_buffer = (short *)datatmp;

    #pragma omp parallel for reduction(min:fmin) reduction(max:fmax) reduction(+:nondatas)    
    for (size_t i = 0; i < total_size; i++) {
      if (src_buffer[i] != fillvalue) {
        float f;
        float rad = scale_factor * src_buffer[i] + add_offset;
        if (is_l1b && datanc->band_id > 0) {
          if (datanc->band_id > 6 && datanc->band_id < 17) { // Bandas térmicas
            f = (rad > 0.0f) ? (planck_fk2 / (log((planck_fk1 / rad) + 1)) - planck_bc1) /
                planck_bc2: 0;
          } else { // Bandas visibles/NIR
            f = kappa0 * rad;
          }
        } else { // Variables L2 ya calibradas que no son de radiancia
          f = rad;
        }
        if (f > fmax)
          fmax = f;
        if (f < fmin)
          fmin = f;
        datanc->fdata.data_in[i] = f;
      } else {
        datanc->fdata.data_in[i] = NonData;
        nondatas++;
      }
    }
    datanc->fdata.fmin = fmin;
    datanc->fdata.fmax = fmax;
    LOG_INFO("Data range: min=%g, max=%g, NonData=%g, invalid_count=%u", fmin,
             fmax, NonData, nondatas);
  }
  free(datatmp);

  printf("Exito decodificando %s!\n", filename);
  return 0;
}

double rad2deg = 180.0 / M_PI;
double hsat, sm_maj, sm_min, lambda_0, H;

void compute_lalo(double x, double y, double *la, double *lo) {
  double snx, sny, csx, csy, sx, sy, sz, rs, a, b, c;
  double sm_maj2 = sm_maj * sm_maj;
  double sm_min2 = sm_min * sm_min;

  snx = sin(x);
  csx = cos(x);
  sny = sin(y);
  csy = cos(y);
  a = snx * snx + csx * csx * (csy * csy + sm_maj2 * sny * sny / sm_min2);
  b = -2.0 * H * csx * csy;
  c = H * H - sm_maj2;
  rs = (-b - sqrt(b * b - 4.0 * a * c)) / (2.0 * a);
  sx = rs * csx * csy;
  sy = -rs * snx;
  sz = rs * csx * sny;

  *la = (double)(atan2(sm_maj2 * sz,
                      sm_min2 * sqrt(((H - sx) * (H - sx)) + (sy * sy))) *
                rad2deg);
  double lon_rad = lambda_0 - atan2(sy, H - sx);

  // Normalizar la longitud al rango [-PI, PI] antes de convertir a grados
  lon_rad = fmod(lon_rad + M_PI, 2.0 * M_PI);
  if (lon_rad < 0)
    lon_rad += 2.0 * M_PI;
  *lo = (double)((lon_rad - M_PI) * rad2deg);
}

int compute_navigation_nc(const char *filename, DataF *navla, DataF *navlo) {
  int ncid, varid;
  int retval;

  if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
    ERR(retval);

  int xid, yid;
  if ((retval = nc_inq_dimid(ncid, "x", &xid)))
    ERR(retval);
  if ((retval = nc_inq_dimid(ncid, "y", &yid)))
    ERR(retval);

  // Recuperamos las dimensiones de los datos
  size_t width, height;
  if ((retval = nc_inq_dimlen(ncid, xid, &width)))
    ERR(retval);
  if ((retval = nc_inq_dimlen(ncid, yid, &height)))
    ERR(retval);
  *navla = dataf_create(width, height);
  navla->size = navla->width * navla->height;
  navlo->width = navla->width;
  navlo->height = navla->height;
  navlo->size = navla->size;

  if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid)))
    ERR(retval);
  nc_get_att_double(ncid, varid, "perspective_point_height", &hsat);
  nc_get_att_double(ncid, varid, "semi_major_axis", &sm_maj);
  nc_get_att_double(ncid, varid, "semi_minor_axis", &sm_min);

  double lo_proj_orig;
  if ((retval = nc_get_att_double(ncid, varid, "longitude_of_projection_origin",
                                 &lo_proj_orig)))
    WRN(retval);
  H = sm_maj + hsat;
  lambda_0 = lo_proj_orig / rad2deg;

  // Obtiene el factor de escala y offset de las VARIABLES (no dimensiones)
  double x_sf, y_sf, x_ao, y_ao;

  // Buscar las variables x e y (no las dimensiones)
  if ((retval = nc_inq_varid(ncid, "x", &xid)))
    ERR(retval);
  if ((retval = nc_inq_varid(ncid, "y", &yid)))
    ERR(retval);

  nc_get_att_double(ncid, xid, "scale_factor", &x_sf);
  nc_get_att_double(ncid, xid, "add_offset", &x_ao);
  nc_get_att_double(ncid, yid, "scale_factor", &y_sf);
  nc_get_att_double(ncid, yid, "add_offset", &y_ao);

  // Leer los arreglos x[] e y[] del NetCDF
  short *x_vals_raw = malloc(width * sizeof(short));
  short *y_vals_raw = malloc(height * sizeof(short));

  if (!x_vals_raw || !y_vals_raw) {
    free(x_vals_raw);
    free(y_vals_raw);
    nc_close(ncid);
    LOG_ERROR("Error de memoria al leer x[], y[]");
    return -1;
  }

  if ((retval = nc_get_var_short(ncid, xid, x_vals_raw))) {
    free(x_vals_raw);
    free(y_vals_raw);
    nc_close(ncid);
    ERR(retval);
  }
  if ((retval = nc_get_var_short(ncid, yid, y_vals_raw))) {
    free(x_vals_raw);
    free(y_vals_raw);
    nc_close(ncid);
    ERR(retval);
  }

  // Apartamos memoria para los datos
  *navlo = dataf_create(width, height);

  size_t k = 0;
  double lomin = 1e10, lamin = 1e10, lomax = -lomin, lamax = -lamin;
  size_t valid_count = 0;

  for (size_t j = 0; j < navla->height; j++) {
    double y = (double)y_vals_raw[j] * y_sf + y_ao;
    for (size_t i = 0; i < navla->width; i++) {
      double la, lo;
      double x = (double)x_vals_raw[i] * x_sf + x_ao;
      compute_lalo(x, y, &la, &lo);
      if (isnan(la) || isnan(lo)) {
        navla->data_in[k] = NonData;
        navlo->data_in[k] = NonData;
      } else {
        if (la < lamin)
          lamin = la;
        if (la > lamax)
          lamax = la;
        if (lo < lomin)
          lomin = lo;
        if (lo > lomax)
          lomax = lo;
        navla->data_in[k] = (float)la;
        navlo->data_in[k] = (float)lo;
        valid_count++;
      }
      k++;
    }
  }

  // Solo actualizar los límites si encontramos al menos un píxel válido
  if (valid_count > 0) {
    navla->fmin = (float)lamin;
    navla->fmax = (float)lamax;
    navlo->fmin = (float)lomin;
    navlo->fmax = (float)lomax;
  } else {
    // Si no hay píxeles válidos, establecer límites por defecto para evitar
    // valores centinela
    navla->fmin = -90.0f;
    navla->fmax = 90.0f;
    navlo->fmin = -180.0f;
    navlo->fmax = 180.0f;
    LOG_WARN("No se encontraron coordenadas válidas en compute_navigation_nc. "
             "Usando límites por defecto.");
  }

  free(x_vals_raw);
  free(y_vals_raw);

  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("Exito creando navegación con %s!\n", filename);
  return 0;
}

/**
 * @brief Crea mallas de navegación (lat/lon) para una cuadrícula geográfica ya
 * existente.
 *
 * En lugar de calcular desde coordenadas geostacionarias, esta función genera
 * las mallas de latitud y longitud por interpolación lineal simple, basándose
 * en los límites geográficos conocidos de la cuadrícula. Es mucho más eficiente
 * para datos ya reproyectados.
 *
 * @param navla Puntero a la estructura DataF para la latitud de salida.
 * @param navlo Puntero a la estructura DataF para la longitud de salida.
 * @param width Ancho de la cuadrícula geográfica.
 * @param height Alto de la cuadrícula geográfica.
 * @param lon_min Longitud mínima.
 * @param lon_max Longitud máxima.
 * @param lat_min Latitud mínima.
 * @param lat_max Latitud máxima.
 * @return 0 en éxito.
 */
int create_navigation_from_reprojected_bounds(DataF *navla, DataF *navlo,
                                              size_t width, size_t height,
                                              float lon_min, float lon_max,
                                              float lat_min, float lat_max) {
  *navla = dataf_create(width, height);
  *navlo = dataf_create(width, height);
  if (navla->data_in == NULL || navlo->data_in == NULL) {
    LOG_FATAL("Falla de memoria al crear mallas de navegación para datos "
              "reproyectados.");
    return -1;
  }

  float lat_range = lat_max - lat_min;
  float lon_range = lon_max - lon_min;

#pragma omp parallel for collapse(2)
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x++) {
      size_t i = y * width + x;
      navlo->data_in[i] = lon_min + ((float)x / (float)(width - 1)) * lon_range;
      navla->data_in[i] =
          lat_max - ((float)y / (float)(height - 1)) * lat_range;
    }
  }
  navla->fmin = lat_min;
  navla->fmax = lat_max;
  navlo->fmin = lon_min;
  navlo->fmax = lon_max;
  return 0;
}

/**
 * @brief Calcula la geometría solar (ángulo cenital y azimutal) para una
 * ubicación y tiempo dados.
 *
 * Usa el mismo algoritmo que sun_zenith_angle en daynight_mask.c para
 * consistencia.
 *
 * @param la Latitud en grados [-90, 90]
 * @param lo Longitud en grados [-180, 180]
 * @param year Año (ej. 2025)
 * @param month Mes [1-12]
 * @param day Día del mes [1-31]
 * @param hour Hora UTC [0-23]
 * @param min Minuto [0-59]
 * @param sec Segundo [0-59]
 * @param zenith_out Puntero para retornar ángulo cenital solar en grados
 * [0-180]
 * @param azimuth_out Puntero para retornar azimut solar en grados [-180, 180]
 */
static void compute_sun_geometry(float la, float lo, int year, int month,
                                 int day, int hour, int min, int sec,
                                 double *zenith_out, double *azimuth_out) {
  // Variables auxiliares
  double t, te, wte, s1, c1, s2, c2, s3, c3, sp, cp, sd, cd, cH, se0, ep, De,
      lambda, epsi, sl, cl, se, ce, L, nu, Dlam;
  int yt, mt;
  double RightAscension, Declination, HourAngle, Zenith, Azimuth;

  double PI = M_PI;
  double PI2 = 2 * M_PI;
  double PIM = M_PI_2;

  double UT = hour + min / 60.0 + sec / 3600.0;
  double Longitude = lo * PI / 180.0;
  double Latitude = la * PI / 180.0;
  double Pressure = 1;
  double Temperature = 0;

  if (month <= 2) {
    mt = month + 12;
    yt = year - 1;
  } else {
    mt = month;
    yt = year;
  }

  t = (double)((int)(365.25 * (double)(yt - 2000)) +
               (int)(30.6001 * (double)(mt + 1)) - (int)(0.01 * (double)(yt)) +
               day) +
      0.0416667 * UT - 21958.0;
  double Dt = 96.4 + 0.00158 * t;
  te = t + 1.1574e-5 * Dt;

  wte = 0.0172019715 * te;

  s1 = sin(wte);
  c1 = cos(wte);
  s2 = 2.0 * s1 * c1;
  c2 = (c1 + s1) * (c1 - s1);
  s3 = s2 * c1 + c2 * s1;
  c3 = c2 * c1 - s2 * s1;

  L = 1.7527901 + 1.7202792159e-2 * te + 3.33024e-2 * s1 - 2.0582e-3 * c1 +
      3.512e-4 * s2 - 4.07e-5 * c2 + 5.2e-6 * s3 - 9e-7 * c3 -
      8.23e-5 * s1 * sin(2.92e-5 * te) + 1.27e-5 * sin(1.49e-3 * te - 2.337) +
      1.21e-5 * sin(4.31e-3 * te + 3.065) +
      2.33e-5 * sin(1.076e-2 * te - 1.533) +
      3.49e-5 * sin(1.575e-2 * te - 2.358) +
      2.67e-5 * sin(2.152e-2 * te + 0.074) +
      1.28e-5 * sin(3.152e-2 * te + 1.547) +
      3.14e-5 * sin(2.1277e-1 * te - 0.488);

  nu = 9.282e-4 * te - 0.8;
  Dlam = 8.34e-5 * sin(nu);
  lambda = L + PI + Dlam;

  epsi = 4.089567e-1 - 6.19e-9 * te + 4.46e-5 * cos(nu);

  sl = sin(lambda);
  cl = cos(lambda);
  se = sin(epsi);
  ce = sqrt(1 - se * se);

  RightAscension = atan2(sl * ce, cl);
  if (RightAscension < 0.0)
    RightAscension += PI2;

  Declination = asin(sl * se);

  HourAngle =
      1.7528311 + 6.300388099 * t + Longitude - RightAscension + 0.92 * Dlam;
  HourAngle = fmod(HourAngle + PI, PI2) - PI;
  if (HourAngle < -PI)
    HourAngle += PI2;

  sp = sin(Latitude);
  cp = sqrt((1 - sp * sp));
  sd = sin(Declination);
  cd = sqrt(1 - sd * sd);
  double sH = sin(HourAngle);
  cH = cos(HourAngle);
  se0 = sp * sd + cp * cd * cH;
  ep = asin(se0) - 4.26e-5 * sqrt(1.0 - se0 * se0);
  Azimuth = atan2(sH, cH * sp - sd * cp / cd);

  if (ep > 0.0)
    De = (0.08422 * Pressure) /
         ((273.0 + Temperature) * tan(ep + 0.003138 / (ep + 0.08919)));
  else
    De = 0.0;

  Zenith = PIM - ep - De;

  if (zenith_out)
    *zenith_out = Zenith * 180.0 / M_PI;
  if (azimuth_out)
    *azimuth_out = Azimuth * 180.0 / M_PI;
}

/**
 * @brief Calcula el ángulo de visión del satélite para un píxel dado.
 *
 * Para satélites geoestacionarios, calcula el ángulo cenital de visión
 * basado en la geometría entre el píxel terrestre y la posición del satélite.
 *
 * @param pixel_lat Latitud del píxel en grados
 * @param pixel_lon Longitud del píxel en grados
 * @param sat_lon Longitud del satélite en grados (subpunto)
 * @param sat_height Altura del satélite sobre el elipsoide en metros
 * @param vza_out Puntero para retornar ángulo cenital de visión en grados
 * [0-90]
 * @param vaa_out Puntero para retornar azimut de visión en grados [-180, 180]
 */
static void compute_satellite_view_angles(float pixel_lat, float pixel_lon,
                                          float sat_lon, float sat_height,
                                          double *vza_out, double *vaa_out) {
  // Constantes del elipsoide WGS84
  const double a = 6378137.0;           // Semi-eje mayor (m)
  const double f = 1.0 / 298.257223563; // Aplanamiento

  // Convertir a radianes
  double lat_rad = pixel_lat * M_PI / 180.0;
  double lon_rad = pixel_lon * M_PI / 180.0;
  double sat_lon_rad = sat_lon * M_PI / 180.0;

  // Posición del píxel en coordenadas cartesianas geocéntricas
  double N = a / sqrt(1.0 - (2.0 * f - f * f) * sin(lat_rad) * sin(lat_rad));
  double x_pixel = N * cos(lat_rad) * cos(lon_rad);
  double y_pixel = N * cos(lat_rad) * sin(lon_rad);
  double z_pixel = N * (1.0 - (2.0 * f - f * f)) * sin(lat_rad);

  // Posición del satélite (en el ecuador, sobre sat_lon)
  double sat_radius = a + sat_height;
  double x_sat = sat_radius * cos(sat_lon_rad);
  double y_sat = sat_radius * sin(sat_lon_rad);
  double z_sat = 0.0; // Satélite geoestacionario en el plano ecuatorial

  // Vector del satélite al píxel (dirección de visión desde el satélite)
  double dx = x_pixel - x_sat;
  double dy = y_pixel - y_sat;
  double dz = z_pixel - z_sat;
  double dist = sqrt(dx * dx + dy * dy + dz * dz);

  // Normalizar el vector de visión
  dx /= dist;
  dy /= dist;
  dz /= dist;

  // Vector normal local en el píxel (apunta hacia arriba desde la superficie)
  double n_len =
      sqrt(x_pixel * x_pixel + y_pixel * y_pixel + z_pixel * z_pixel);
  double nx = x_pixel / n_len;
  double ny = y_pixel / n_len;
  double nz = z_pixel / n_len;

  // Ángulo cenital de visión: ángulo entre la dirección de visión y la normal
  // cos(VZA) = -dot(view_direction, surface_normal)
  // El signo negativo porque view_direction apunta hacia el píxel (hacia abajo)
  double cos_vza = -(dx * nx + dy * ny + dz * nz);
  double vza = acos(fmax(-1.0, fmin(1.0, cos_vza))) * 180.0 / M_PI;

  // Azimut de visión: proyección en el plano local
  // Sistema local: Este-Norte-Arriba (ENU)
  double east_x = -sin(lon_rad);
  double east_y = cos(lon_rad);
  double east_z = 0.0;

  double north_x = -sin(lat_rad) * cos(lon_rad);
  double north_y = -sin(lat_rad) * sin(lon_rad);
  double north_z = cos(lat_rad);

  double view_east = dx * east_x + dy * east_y + dz * east_z;
  double view_north = dx * north_x + dy * north_y + dz * north_z;
  double vaa = atan2(view_east, view_north) * 180.0 / M_PI;

  if (vza_out)
    *vza_out = vza;
  if (vaa_out)
    *vaa_out = vaa;
}

/**
 * @brief Calcula mapas de geometría solar (SZA y SAA) para toda la imagen.
 *
 * Esta función procesa píxel por píxel usando las coordenadas geográficas
 * precalculadas y los metadatos temporales del archivo NetCDF.
 *
 * @param filename Ruta al archivo NetCDF GOES L1b
 * @param navla Malla de latitudes (ya calculada)
 * @param navlo Malla de longitudes (ya calculada)
 * @param sza Estructura DataF de salida para Solar Zenith Angle
 * @param saa Estructura DataF de salida para Solar Azimuth Angle
 * @return 0 en éxito, ERRCODE en error
 */
int compute_solar_angles_nc(const char *filename, const DataF *navla,
                            const DataF *navlo, DataF *sza, DataF *saa) {
  int ncid, retval;

  if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
    ERR(retval);

  // Leer metadatos de tiempo
  int time_varid;
  if ((retval = nc_inq_varid(ncid, "t", &time_varid)))
    ERR(retval);
  double tiempo;
  if ((retval = nc_get_var_double(ncid, time_varid, &tiempo)))
    ERR(retval);

  LOG_DEBUG("Tiempo J2000 leído del NetCDF: %.1f segundos", tiempo);

  // Convertir tiempo J2000 a fecha/hora
  long tt = (long)(tiempo);
  time_t dt = 946728000 + tt; // Epoch J2000
  struct tm ts = *gmtime(&dt);
  int year = ts.tm_year + 1900;
  int month = ts.tm_mon + 1;
  int day = ts.tm_mday;
  int hour = ts.tm_hour;
  int min = ts.tm_min;
  int sec = ts.tm_sec;

  if ((retval = nc_close(ncid)))
    ERR(retval);

  // Log de fecha/hora para debugging
  LOG_DEBUG("Fecha/hora para cálculo solar: %04d-%02d-%02d %02d:%02d:%02d UTC",
            year, month, day, hour, min, sec);

  // Crear estructuras de salida
  *sza = dataf_create(navla->width, navla->height);
  *saa = dataf_create(navla->width, navla->height);

  if (sza->data_in == NULL || saa->data_in == NULL) {
    LOG_FATAL("Falla de memoria al crear mapas de ángulos solares.");
    return ERRCODE;
  }

  LOG_INFO("Calculando geometría solar para %04d-%02d-%02d %02d:%02d:%02d UTC",
           year, month, day, hour, min, sec);

  double start_time = omp_get_wtime();

// Calcular para cada píxel
#pragma omp parallel for
  for (size_t i = 0; i < navla->size; i++) {
    float la = navla->data_in[i];
    float lo = navlo->data_in[i];

    if (la == NonData || lo == NonData) {
      sza->data_in[i] = NonData;
      saa->data_in[i] = NonData;
    } else {
      double zenith, azimuth;
      compute_sun_geometry(la, lo, year, month, day, hour, min, sec, &zenith,
                           &azimuth);
      sza->data_in[i] = (float)zenith;
      saa->data_in[i] = (float)azimuth;
    }
  }

  double elapsed = omp_get_wtime() - start_time;
  LOG_INFO("Geometría solar calculada en %.3f segundos.", elapsed);

  return 0;
}

/**
 * @brief Calcula mapas de geometría del satélite (VZA y VAA) para toda la
 * imagen.
 *
 * Para satélites geoestacionarios GOES, calcula los ángulos de visión desde
 * el satélite hacia cada píxel.
 *
 * @param filename Ruta al archivo NetCDF GOES L1b
 * @param navla Malla de latitudes (ya calculada)
 * @param navlo Malla de longitudes (ya calculada)
 * @param vza Estructura DataF de salida para View Zenith Angle
 * @param vaa Estructura DataF de salida para View Azimuth Angle
 * @return 0 en éxito, ERRCODE en error
 */
int compute_satellite_angles_nc(const char *filename, const DataF *navla,
                                const DataF *navlo, DataF *vza, DataF *vaa) {
  int ncid, varid, retval;

  if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
    ERR(retval);

  // Leer parámetros del satélite
  if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid)))
    ERR(retval);

  float sat_lon, sat_height_km;
  if ((retval = nc_get_att_float(ncid, varid, "longitude_of_projection_origin",
                                 &sat_lon)))
    ERR(retval);
  if ((retval = nc_get_att_float(ncid, varid, "perspective_point_height",
                                 &sat_height_km)))
    ERR(retval);

  if ((retval = nc_close(ncid)))
    ERR(retval);

  LOG_INFO(
      "Calculando geometría del satélite (subpunto: %.1f°E, altura: %.0f km)",
      sat_lon, sat_height_km);

  // Crear estructuras de salida
  *vza = dataf_create(navla->width, navla->height);
  *vaa = dataf_create(navla->width, navla->height);

  if (vza->data_in == NULL || vaa->data_in == NULL) {
    LOG_FATAL("Falla de memoria al crear mapas de ángulos del satélite.");
    return ERRCODE;
  }

  double start_time = omp_get_wtime();

// Calcular para cada píxel
#pragma omp parallel for
  for (size_t i = 0; i < navla->size; i++) {
    float la = navla->data_in[i];
    float lo = navlo->data_in[i];

    if (la == NonData || lo == NonData) {
      vza->data_in[i] = NonData;
      vaa->data_in[i] = NonData;
    } else {
      double view_zenith, view_azimuth;
      compute_satellite_view_angles(la, lo, sat_lon, sat_height_km,
                                    &view_zenith, &view_azimuth);
      vza->data_in[i] = (float)view_zenith;
      vaa->data_in[i] = (float)view_azimuth;
    }
  }

  double elapsed = omp_get_wtime() - start_time;
  LOG_INFO("Geometría del satélite calculada en %.3f segundos.", elapsed);

  return 0;
}

/**
 * @brief Calcula el azimut relativo entre el sol y el satélite.
 *
 * El azimut relativo (RAA) es la diferencia angular entre el azimut solar
 * y el azimut de visión del satélite, normalizado al rango [0, 180].
 *
 * @param saa Mapa de Solar Azimuth Angle (entrada)
 * @param vaa Mapa de View Azimuth Angle (entrada)
 * @param raa Mapa de Relative Azimuth Angle (salida)
 */
void compute_relative_azimuth(const DataF *saa, const DataF *vaa, DataF *raa) {
  *raa = dataf_create(saa->width, saa->height);

  if (raa->data_in == NULL) {
    LOG_FATAL("Falla de memoria al crear mapa de azimut relativo.");
    return;
  }

#pragma omp parallel for
  for (size_t i = 0; i < saa->size; i++) {
    float sa = saa->data_in[i];
    float va = vaa->data_in[i];

    if (sa == NonData || va == NonData) {
      raa->data_in[i] = NonData;
    } else {
      // Diferencia absoluta
      float diff = fabsf(sa - va);

      // Normalizar al rango [0, 180]
      if (diff > 180.0f) {
        diff = 360.0f - diff;
      }

      raa->data_in[i] = diff;
    }
  }

  LOG_INFO("Azimut relativo calculado para %zu píxeles.", raa->size);
}
