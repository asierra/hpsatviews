/* NetCDF Data reader
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
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
  if ((retval = nc_inq_dimlen(ncid, xid, &width))) ERR(retval);
  if ((retval = nc_inq_dimlen(ncid, yid, &height))) ERR(retval);
  total_size = width * height;
  LOG_INFO("NetCDF dimensions: %lux%lu (total: %lu)", width,
           height, total_size);

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

  size_t type_size = (var_type == NC_BYTE) ? sizeof(signed char) : sizeof(short);
  void *datatmp = malloc(type_size * total_size);
  if (datatmp == NULL) {
    LOG_FATAL("Failed to allocate memory for NetCDF data");
    return ERRCODE;
  }
  if ((retval = nc_get_var(ncid, rad_varid, datatmp))) // Lectura genérica
    ERR(retval);

  // Obtenemos el tiempo
  int time_varid;
  if ((retval = nc_inq_varid(ncid, "t", &time_varid)))
    ERR(retval);
  double tiempo;
  if ((retval = nc_get_var_double(ncid, time_varid, &tiempo)))
    ERR(retval);
  long tt = (long)(tiempo);
  time_t dt = 946728000 + tt;
  char buf[80];
  struct tm ts = *gmtime(&dt);
  strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", &ts);
  printf("%s\n", buf);
  datanc->year = ts.tm_year + 1900;
  datanc->mon = ts.tm_mon + 1;
  datanc->day = ts.tm_mday;
  datanc->hour = ts.tm_hour;
  datanc->min = ts.tm_min;
  datanc->sec = ts.tm_sec;

  // Only if L1b
  float planck_fk1, planck_fk2, planck_bc1, planck_bc2, kappa0;
  if (strcmp(variable, "Rad") == 0) {

    int band_id_varid;
    if ((retval = nc_inq_varid(ncid, "band_id", &band_id_varid)))
      ERR(retval);
    if ((retval = nc_get_var_ubyte(ncid, band_id_varid, &datanc->band_id)))
      ERR(retval);
    LOG_DEBUG("NetCDF band ID: %d", datanc->band_id);

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
      printf("Planck %g %g %g %g\n", planck_fk1, planck_fk2, planck_bc1,
             planck_bc2);
    } else if (datanc->band_id >= 1) {
      int kappa0_varid;
      if ((retval = nc_inq_varid(ncid, "kappa0", &kappa0_varid)))
        ERR(retval);
      if ((retval = nc_get_var_float(ncid, kappa0_varid, &kappa0)))
        ERR(retval);
      printf("Kappa0 %g\n", kappa0);
    }
  } else {
    datanc->band_id = 0;
  }

  if ((retval = nc_close(ncid)))
    ERR(retval);

  // Aplica factor de escala, offset y parámetros
  // Calcula máximos y mínimos para verificar datos
  float fmin = 1e20;
  float fmax = -fmin;
  unsigned nondatas = 0;

  if (var_type == NC_BYTE) {
    // --- RUTA PARA DATOS TIPO BYTE (ej. Cloud Phase) ---
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
    datanc->is_float = true;
    datanc->fdata = dataf_create(width, height);
    if (datanc->fdata.data_in == NULL) {
      LOG_FATAL("Memory allocation failed for float data buffer");
      free(datatmp);
      return ERRCODE;
    }
    short *src_buffer = (short *)datatmp;

    for (size_t i = 0; i < total_size; i++) {
      if (src_buffer[i] != fillvalue) {
        float f;
        float rad = scale_factor * src_buffer[i] + add_offset;
        if (datanc->band_id > 0) {
          if (datanc->band_id > 6 && datanc->band_id < 17) { // Bandas térmicas
            f = (planck_fk2 / (log((planck_fk1 / rad) + 1)) - planck_bc1) /
                planck_bc2;
          } else { // Bandas visibles/NIR
            f = kappa0 * rad;
          }
        } else { // Variables L2 que no son de radiancia (ej. LST)
          f = rad;
        }
        if (f > fmax) fmax = f;
        if (f < fmin) fmin = f;
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

// Carga un conjunto de datos en NetCDF y lo pone en una estructura DataF
int load_nc_float(const char *filename, DataF *datanc, const char *variable) {
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
  if ((retval = nc_inq_dimlen(ncid, xid, &datanc->width)))
    ERR(retval);
  if ((retval = nc_inq_dimlen(ncid, yid, &datanc->height)))
    ERR(retval);
  datanc->size = datanc->width * datanc->height;
  printf("Dimensiones x %lu y %lu\n", datanc->width, datanc->height);

  // Obtenemos el id de la variable
  if ((retval = nc_inq_varid(ncid, variable, &varid)))
    ERR(retval);

  // Apartamos memoria para los datos
  datanc->data_in = malloc(sizeof(float) * datanc->size); // This function seems unused, but let's fix it.

  // Recupera los datos
  if ((retval = nc_get_var_float(ncid, varid, datanc->data_in)))
    ERR(retval);

  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("Exito decodificando %s!\n", filename);
  return 0;
}

double rad2deg = 180.0 / M_PI;
float hsat, sm_maj, sm_min, lambda_0, H;

void compute_lalo(float x, float y, float *la, float *lo) {
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

  *la = (float)(atan2(sm_maj2 * sz,
                      sm_min2 * sqrt(((H - sx) * (H - sx)) + (sy * sy))) *
                rad2deg);
  double lon_rad = lambda_0 - atan2(sy, H - sx);
  
  // Normalizar la longitud al rango [-PI, PI] antes de convertir a grados
  lon_rad = fmod(lon_rad + M_PI, 2.0 * M_PI);
  if (lon_rad < 0) lon_rad += 2.0 * M_PI;
  *lo = (float)((lon_rad - M_PI) * rad2deg);
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
  if ((retval = nc_inq_dimlen(ncid, xid, &width))) ERR(retval);
  if ((retval = nc_inq_dimlen(ncid, yid, &height))) ERR(retval);
  *navla = dataf_create(width, height);
  navla->size = navla->width * navla->height;
  navlo->width = navla->width;
  navlo->height = navla->height;
  navlo->size = navla->size;
  // printf("Dimensiones x %lu y %lu total %lu\n", navla->width,
  // navla->height,
  //        navla->size);

  if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid)))
    ERR(retval);
  if ((retval =
           nc_get_att_float(ncid, varid, "perspective_point_height", &hsat)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, varid, "semi_major_axis", &sm_maj)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, varid, "semi_minor_axis", &sm_min)))
    WRN(retval);
  float lo_proj_orig;
  if ((retval = nc_get_att_float(ncid, varid, "longitude_of_projection_origin",
                                 &lo_proj_orig)))
    WRN(retval);
  H = sm_maj + hsat;
  lambda_0 = lo_proj_orig / rad2deg;
  // printf("hsat %g %g %g %g %g\n", hsat, sm_maj, sm_min, lo_proj_orig, H);

  // Obtiene el factor de escala y offset de las VARIABLES (no dimensiones)
  float x_sf, y_sf, x_ao, y_ao;
  int x_varid, y_varid;
  
  // Buscar las variables x e y (no las dimensiones)
  if ((retval = nc_inq_varid(ncid, "x", &x_varid)))
    ERR(retval);
  if ((retval = nc_inq_varid(ncid, "y", &y_varid)))
    ERR(retval);
    
  if ((retval = nc_get_att_float(ncid, x_varid, "scale_factor", &x_sf)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, x_varid, "add_offset", &x_ao)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, y_varid, "scale_factor", &y_sf)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, y_varid, "add_offset", &y_ao)))
    WRN(retval);

  // Apartamos memoria para los datos
  *navlo = dataf_create(width, height);

  int k = 0;
  float lomin = 1e10, lamin = 1e10, lomax = -lomin, lamax = -lamin;
  int valid_count = 0;
  
  for (int j = 0; j < navla->height; j++) {
    float y = j * y_sf + y_ao;
    for (int i = 0; i < navla->width; i++) {
      float la, lo;
      float x = i * x_sf + x_ao;
      compute_lalo(x, y, &la, &lo);
      if (isnan(la) || isnan(lo)) {
        // printf("Is nan %g\n", la);
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
        navla->data_in[k] = la;
        navlo->data_in[k] = lo;
        valid_count++;
      }
      k++;
    }
  }
  
  // Solo actualizar los límites si encontramos al menos un píxel válido
  if (valid_count > 0) {
    navla->fmin = lamin;
    navla->fmax = lamax;
    navlo->fmin = lomin;
    navlo->fmax = lomax;
  } else {
    // Si no hay píxeles válidos, establecer límites por defecto para evitar valores centinela
    navla->fmin = -90.0f;
    navla->fmax = 90.0f;
    navlo->fmin = -180.0f;
    navlo->fmax = 180.0f;
    LOG_WARN("No se encontraron coordenadas válidas en compute_navigation_nc. Usando límites por defecto.");
  }
  
  printf("corners %g %g  %g %g (valid: %d/%lu)\n", lomin, lamin, lomax, lamax, valid_count, navla->size);
  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("Exito creando navegación con %s!\n", filename);
  return 0;
}

/**
 * @brief Crea mallas de navegación (lat/lon) para una cuadrícula geográfica ya existente.
 *
 * En lugar de calcular desde coordenadas geostacionarias, esta función genera las mallas
 * de latitud y longitud por interpolación lineal simple, basándose en los límites
 * geográficos conocidos de la cuadrícula. Es mucho más eficiente para datos ya reproyectados.
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
int create_navigation_from_reprojected_bounds(DataF *navla, DataF *navlo, size_t width, size_t height, float lon_min, float lon_max, float lat_min, float lat_max) {
    *navla = dataf_create(width, height);
    *navlo = dataf_create(width, height);
    if (navla->data_in == NULL || navlo->data_in == NULL) {
        LOG_FATAL("Falla de memoria al crear mallas de navegación para datos reproyectados.");
        return -1;
    }

    float lat_range = lat_max - lat_min;
    float lon_range = lon_max - lon_min;

    #pragma omp parallel for collapse(2)
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t i = y * width + x;
            navlo->data_in[i] = lon_min + ( (float)x / (float)(width - 1) ) * lon_range;
            navla->data_in[i] = lat_max - ( (float)y / (float)(height - 1) ) * lat_range;
        }
    }
    navla->fmin = lat_min; navla->fmax = lat_max;
    navlo->fmin = lon_min; navlo->fmax = lon_max;
    return 0;
}

/*
 * Carga una variable 2D genérica de un NetCDF en una estructura DataF.
 * Asume que las dimensiones son [y, x] o [height, width].
 */
DataF dataf_load_from_netcdf(const char *filename, const char *varname) {
  DataF data = {0}; // Inicializa la estructura a cero
  int ncid, retval, varid, ndims;
  int dimids[NC_MAX_VAR_DIMS];
  size_t height, width;

  if ((retval = nc_open(filename, NC_NOWRITE, &ncid))) {
    LOG_ERROR("NetCDF error abriendo %s: %s", filename, nc_strerror(retval));
    return data; // Retorna estructura vacía
  }

  if ((retval = nc_inq_varid(ncid, varname, &varid))) {
    LOG_ERROR("No se encontró la variable '%s' en %s: %s", varname, filename,
              nc_strerror(retval));
    nc_close(ncid);
    return data;
  }

  if ((retval = nc_inq_varndims(ncid, varid, &ndims))) {
    LOG_ERROR("Error al leer ndims para %s: %s", varname, nc_strerror(retval));
    nc_close(ncid);
    return data;
  }

  if (ndims != 2) {
    LOG_ERROR("La variable '%s' no es 2D (tiene %d dims).", varname, ndims);
    nc_close(ncid);
    return data;
  }

  if ((retval = nc_inq_vardimid(ncid, varid, dimids))) {
    LOG_ERROR("Error al leer dimids para %s: %s", varname, nc_strerror(retval));
    nc_close(ncid);
    return data;
  }

  // Asume que las dimensiones son [y, x]
  if ((retval = nc_inq_dimlen(ncid, dimids[0], &height))) { /*...*/
  }
  if ((retval = nc_inq_dimlen(ncid, dimids[1], &width))) { /*...*/
  }

  // Crear la estructura DataF
  data = dataf_create(width, height);
  if (data.data_in == NULL) {
    LOG_FATAL("Falla de memoria al crear DataF para %s", varname);
    nc_close(ncid);
    return data;
  }

  // Leer los datos
  if ((retval = nc_get_var_float(ncid, varid, data.data_in))) {
    LOG_ERROR("Error al leer datos de '%s': %s", varname, nc_strerror(retval));
    dataf_destroy(&data); // Libera memoria si la lectura falla
  }

  // TODO: Leer NonData/_FillValue y fmin/fmax si es necesario

  nc_close(ncid);
  return data;
}