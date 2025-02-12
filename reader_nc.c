/* NetCDF Data reader
 * Copyright (c) 2019  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include <math.h>
#include <netcdf.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERRCODE 2
#define ERR(e)                                                                 \
  {                                                                            \
    printf("Error: %s\n", nc_strerror(e));                                     \
    return (ERRCODE);                                                          \
  }
#define WRN(e)                                                                 \
  { printf("Warning: %s\n", nc_strerror(e)); }

// Carga un conjunto de datos con factor de escala en NetCDF y lo pone en una
// estructura DataNC
int load_nc_sf(char *filename, DataNC *datanc, char *variable) {
  int ncid, varid;
  int x, y, retval;

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
  printf("Dimensiones x %d y %d\n", datanc->width, datanc->height);

  // Obtenemos el id de la variable
  if ((retval = nc_inq_varid(ncid, variable, &varid)))
    ERR(retval);

  // Obtiene el factor de escala y offset de la variable
  if ((retval = nc_get_att_float(ncid, varid, "scale_factor",
                                 &datanc->scale_factor)))
    WRN(retval);
  if ((retval =
           nc_get_att_float(ncid, varid, "add_offset", &datanc->add_offset)))
    WRN(retval);
  printf("Scale %g Offset %g\n", datanc->scale_factor, datanc->add_offset);

  // Apartamos memoria para los datos
  datanc->data_in = malloc(sizeof(short) * datanc->size);

  // Recupera los datos
  if ((retval = nc_get_var_short(ncid, varid, datanc->data_in)))
    ERR(retval);

  // Obtenemos el tiempo
  double tiempo;
  if ((retval = nc_inq_varid(ncid, "t", &varid)))
    ERR(retval);
  if ((retval = nc_get_var_double(ncid, varid, &tiempo)))
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
  printf("Fecha %d %d %d\n", datanc->year, datanc->mon, datanc->day);

  if ((retval = nc_inq_varid(ncid, "band_id", &varid)))
      ERR(retval);
  if ((retval = nc_get_var_ubyte(ncid, varid, &datanc->band_id)))
      ERR(retval);
  printf("banda %d\n", datanc->band_id);

  // Obtiene los parámetros de Planck
  if (datanc->band_id > 6) {
    if ((retval = nc_inq_varid(ncid, "planck_fk1", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &datanc->planck_fk1)))
      ERR(retval);
    if ((retval = nc_inq_varid(ncid, "planck_fk2", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &datanc->planck_fk2)))
      ERR(retval);
    if ((retval = nc_inq_varid(ncid, "planck_bc1", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &datanc->planck_bc1)))
      ERR(retval);
    if ((retval = nc_inq_varid(ncid, "planck_bc2", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &datanc->planck_bc2)))
      ERR(retval);
    printf("Planck %g %g %g %g\n", datanc->planck_fk1, datanc->planck_fk2,
           datanc->planck_bc1, datanc->planck_bc2);
  } else if (datanc->band_id >= 1) {
    if ((retval = nc_inq_varid(ncid, "kappa0", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &datanc->kappa0)))
      ERR(retval);
    printf("Kappa0 %g\n", datanc->kappa0);
  }

  if ((retval = nc_close(ncid)))
    ERR(retval);

  // Aplica factor de escala y offset
  // Calcula máximos y mínimos para verificar datos

  printf("sizes %d %d\n", sizeof(float), sizeof(short));
  int imin = 20000;
  int imax = -20000;
  int neg = 0;
  for (int i = 0; i < datanc->size; i++)
    if (datanc->data_in[i] >= 0) {
      if (datanc->data_in[i] > imax)
        imax = datanc->data_in[i];
      if (datanc->data_in[i] < imin) {
        imin = datanc->data_in[i];
      }
    } else if (datanc->data_in[i] < neg)
      neg = datanc->data_in[i];
  printf("min %d max %d neg %d\n", imin, imax, neg);

  printf("Exito decodificando %s!\n", filename);
  return 0;
}

// Carga un conjunto de datos en NetCDF y lo pone en una estructura DataNC
int load_nc_float(char *filename, DataNCF *datanc, char *variable) {
  int ncid, varid;
  int x, y, retval;

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
  printf("Dimensiones x %d y %d\n", datanc->width, datanc->height);

  // Obtenemos el id de la variable
  if ((retval = nc_inq_varid(ncid, variable, &varid)))
    ERR(retval);

  // Apartamos memoria para los datos
  datanc->data_in = malloc(sizeof(float) * datanc->size);

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

int compute_lalo(float x, float y, float *la, float *lo) {
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

  rs = (-1.0 * b - sqrt(b * b - 4.0 * a * c)) / (2.0 * a);

  sx = rs * csx * csy;
  sy = -rs * snx;
  sz = rs * csx * csy;

  *la = (float)(atan2(sm_maj2 * sz,
                      sm_min2 * sqrt(((H - sx) * (H - sx)) + (sy * sy))) *
                rad2deg);
  *lo = (float)(lambda_0 - atan2(sy, H - sx) * rad2deg);
}

int compute_navigation_nc(char *filename, DataNCF *navla, DataNCF *navlo) {
  int ncid, varid;
  int x, y, retval;

  if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
    ERR(retval);

  int xid, yid;
  if ((retval = nc_inq_dimid(ncid, "x", &xid)))
    ERR(retval);
  if ((retval = nc_inq_dimid(ncid, "y", &yid)))
    ERR(retval);

  // Recuperamos las dimensiones de los datos
  if ((retval = nc_inq_dimlen(ncid, xid, &navla->width)))
    ERR(retval);
  if ((retval = nc_inq_dimlen(ncid, yid, &navla->height)))
    ERR(retval);
  navla->size = navla->width * navla->height;
  navlo->width = navla->width;
  navlo->height = navla->height;
  navlo->size = navla->size;
  printf("Dimensiones x %d y %d total %d\n", navla->width, navla->height,
         navla->size);

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
  printf("hsat %g %g %g %g %g\n", hsat, sm_maj, sm_min, lo_proj_orig, H);

  // Obtiene el factor de escala y offset de la variable
  float x_sf, y_sf, x_ao, y_ao;
  if ((retval = nc_get_att_float(ncid, xid, "scale_factor", &x_sf)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, xid, "add_offset", &x_ao)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, yid, "scale_factor", &y_sf)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, yid, "add_offset", &y_ao)))
    WRN(retval);

  // Apartamos memoria para los datos
  navla->data_in = malloc(sizeof(float) * navla->size);
  navlo->data_in = malloc(sizeof(float) * navlo->size);

  int k = 0;
  for (int j = 0; j < navla->height; j++) {
    float y = j * y_sf + y_ao;
    // int k = j*navla->width;
    // printf("y %d %g - ", j); fflush(stdout);
    for (int i = 0; i < navla->width; i++) {
      // k += i;
      float x = i * x_sf + x_ao;
      // printf("y %d %g %d %g %d - ", j, y, i, x, k); fflush(stdout);
      compute_lalo(x, y, &navla->data_in[k], &navlo->data_in[k]);
      k++;
    }
  }

  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("Exito creando navegación con %s!\n", filename);
  return 0;
}
