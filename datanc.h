#ifndef _DATANC_H_
#define _DATANC_H_


// En esta estructura se guardanlos datos NetCDF recuperados de disco
typedef struct {
  size_t width, height;
  size_t size;
  short *data_in;
  float scale_factor;
  float add_offset;
  float planck_fk1, planck_fk2, planck_bc1, planck_bc2;
  int year, mon, day, hour, min, sec;
} DataNC;

typedef struct {
  size_t width, height;
  size_t size;
  float *data_in;
} DataNCF;

// Remuestreo seleccionando vecinos cercanos con factor entero
DataNC downsample_neighbor_nc(DataNC datanc_big, int factor);

#endif
