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


// Carga un conjunto de datos de NetCDF y lo pone en una estructura DataNC
int load_nc_sf(char *filename, char *variable, DataNC *datanc) {
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
  printf("Dimensiones x %d y %d  size %d\n", datanc->width, datanc->height, datanc->size);

  // Obtenemos el id de la variable
  if ((retval = nc_inq_varid(ncid, variable, &varid)))
    ERR(retval);

  // Obtiene el factor de escala y offset de la variable
  float scale_factor, add_offset;
  if ((retval=nc_get_att_float(ncid, varid, "scale_factor", &scale_factor)))
    WRN(retval);
  if ((retval=nc_get_att_float(ncid, varid, "add_offset", &add_offset)))
    WRN(retval);
  printf("Scale %g Offset %g\n", scale_factor, add_offset);

  // Recupera los datos
  short *datatmp = malloc(sizeof(short) * datanc->size);
  if ((retval = nc_get_var_short(ncid, varid, datatmp)))
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
  //printf("Fecha %d %d %d\n", datanc->year, datanc->mon, datanc->day);

  if ((retval = nc_inq_varid(ncid, "band_id", &varid)))
      ERR(retval);
  if ((retval = nc_get_var_ubyte(ncid, varid, &datanc->band_id)))
      ERR(retval);
  printf("banda %d\n", datanc->band_id);

  // Obtiene los parámetros de Planck o Kappa0
  float planck_fk1, planck_fk2, planck_bc1, planck_bc2, kappa0;
  if (datanc->band_id > 6) {
    if ((retval = nc_inq_varid(ncid, "planck_fk1", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &planck_fk1)))
      ERR(retval);
    if ((retval = nc_inq_varid(ncid, "planck_fk2", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &planck_fk2)))
      ERR(retval);
    if ((retval = nc_inq_varid(ncid, "planck_bc1", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &planck_bc1)))
      ERR(retval);
    if ((retval = nc_inq_varid(ncid, "planck_bc2", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &planck_bc2)))
      ERR(retval);
    printf("Planck %g %g %g %g\n", planck_fk1, planck_fk2,
           planck_bc1, planck_bc2);
  } else if (datanc->band_id >= 1) {
    if ((retval = nc_inq_varid(ncid, "kappa0", &varid)))
      ERR(retval);
    if ((retval = nc_get_var_float(ncid, varid, &kappa0)))
      ERR(retval);
    printf("Kappa0 %g\n", kappa0);
  }

  if ((retval = nc_close(ncid)))
    ERR(retval);

  // Aplica factor de escala, offset y parámetros
  // Calcula máximos y mínimos para verificar datos
  float fmin = 1e20;
  float fmax = -fmin;
  int neg = 0;
  datanc->data_in = malloc(sizeof(float)*datanc->size);
  for (int i = 0; i < datanc->size; i++)
    if (datatmp[i] >= 0) {
      float f;
      float rad = scale_factor * datatmp[i] + add_offset;
      if (datanc->band_id > 6 && datanc->band_id < 17) {
        f = (planck_fk2 / (log((planck_fk1 / rad) + 1)) -
                   planck_bc1) / planck_bc2;
      } else {
        f = kappa0*rad;
      }
      if (f > fmax)
        fmax = f;
      if (f < fmin) {
        fmin = f;
      }
      datanc->data_in[i] = f;
    } else if (datatmp[i] < neg)
      neg = datatmp[i];
  printf("min %g max %g neg %d\n", fmin, fmax, neg);
  free(datatmp);

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
  double sm_maj2 = sm_maj*sm_maj;
  double sm_min2 = sm_min*sm_min;

  snx = sin(x);
  csx = cos(x);
  sny = sin(y);
  csy = cos(y);
  a = snx*snx + csx*csx * (csy*csy + sm_maj2*sny*sny/sm_min2);
  b = -2.0*H*csx*csy;
  c = H*H - sm_maj2;
  rs = (-b - sqrt(b*b - 4.0*a*c)) / (2.0*a);
  sx = rs*csx*csy;
  sy = -rs*snx;
  sz = rs*csx*sny;

  *la = (float)(atan2(sm_maj2 * sz,
                      sm_min2 * sqrt(((H - sx) * (H - sx)) + (sy * sy))) *
                rad2deg);
  *lo = (float)((lambda_0 - atan2(sy, H - sx))*rad2deg);
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
  float lomin=1e10, lamin=1e10, lomax=-lomin, lamax=-lamin;
  for (int j = 0; j < navla->height; j++) {
    float y = j * y_sf + y_ao;
    for (int i = 0; i < navla->width; i++) {
      float la, lo;
      float x = i * x_sf + x_ao;
      compute_lalo(x, y, &la, &lo);
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
      k++;
    }

  }
  printf("corners %g %g  %g %g\n", lomin, lamin, lomax, lamax);
  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("Exito creando navegación con %s!\n", filename);
  return 0;
}
