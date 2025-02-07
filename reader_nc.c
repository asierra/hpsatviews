/* NetCDF Data reader
 * Copyright (c) 2019  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM 
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netcdf.h>
#include <omp.h>
#include <time.h>
#include "datanc.h"


#define ERRCODE 2
#define ERR(e) {printf("Error: %s\n", nc_strerror(e)); return(ERRCODE);}
#define WRN(e) {printf("Warning: %s\n", nc_strerror(e)); }


// Carga un conjunto de datos con factor de escala en NetCDF y lo pone en una estructura DataNC
int load_nc_sf(char *filename, DataNC *datanc, char *variable, int planck) 
{
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
  if ((retval = nc_get_att_float(ncid, varid, "scale_factor", &datanc->scale_factor)))
    WRN(retval);
  if ((retval = nc_get_att_float(ncid, varid, "add_offset", &datanc->add_offset)))
    WRN(retval);
  printf("Scale %g Offset %g\n", datanc->scale_factor, datanc->add_offset);
 
  // Apartamos memoria para los datos 
  datanc->data_in = malloc(sizeof(short)*datanc->size);

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
  char       buf[80];
  struct tm  ts = *gmtime(&dt);
  strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", &ts);
  printf("%s\n", buf);
  datanc->year = ts.tm_year + 1900;
  datanc->mon = ts.tm_mon + 1;
  datanc->day = ts.tm_mday;
  datanc->hour = ts.tm_hour;
  datanc->min = ts.tm_min;
  datanc->sec = ts.tm_sec;
  printf("Fecha %d %d %d\n",  datanc->year, datanc->mon, datanc->day);
 // Obtiene los parámetros de Planck
  if (planck) {
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
    printf("Planck %g %g %g %g\n", datanc->planck_fk1, datanc->planck_fk2, datanc->planck_bc1, datanc->planck_bc2);
  }

  if ((retval = nc_close(ncid)))
    ERR(retval);

  // Aplica factor de escala y offset
  // Calcula máximos y mínimos para verificar datos
  int imin =  20000;
  int imax = -20000;
  int neg = 0;
  for (int i=0; i < datanc->size; i++)
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
int load_nc_float(char *filename, DataNCF *datanc, char *variable) 
{
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
  datanc->data_in = malloc(sizeof(float)*datanc->size);

  // Recupera los datos
  if ((retval = nc_get_var_float(ncid, varid, datanc->data_in)))
    ERR(retval);
  
  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("Exito decodificando %s!\n", filename);
  return 0;
}

// Remuestreo seleccionando vecinos cercanos con factor entero
DataNC downsample_neighbor_nc(DataNC datanc_big, int factor)
{
  DataNC datanc;
  datanc.add_offset   = datanc_big.add_offset;
  datanc.scale_factor = datanc_big.scale_factor;
  datanc.width  = datanc_big.width/factor;
  datanc.height = datanc_big.height/factor;
  datanc.size = datanc.width * datanc.height;
  datanc.data_in = malloc(sizeof(short)*datanc.size);
  
  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datanc_big.height; j += factor)
    for (int i=0; i < datanc_big.width; i += factor) {
      int is = (j*datanc.width + i)/factor;
      datanc.data_in[is] = datanc_big.data_in[j*datanc_big.width + i];
    }
  double end = omp_get_wtime();
  printf("Tiempo downsampling %lf\n", end - start);

  return datanc;
}
