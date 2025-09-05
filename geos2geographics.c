/* Creates a dataset in geographics from a geostationary dataset.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "reader_nc.h"
#include "writer_png.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>

float dataf_value(DataF data, unsigned i, unsigned j) {
  unsigned ii = j*data.width + i;
  return data.data_in[ii];
}

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha);

unsigned laheight, lowidth;
float lamin, lamax, lomin, lomax;

void getindices(float la, float lo, unsigned *i, unsigned *j) {
  *i = (unsigned)((lowidth - 1) * (lo - lomin) / (lomax - lomin));
  *j = (unsigned)((laheight - 1) * (la - lamin) / (lamax - lamin));
}

DataF geos2geographics(DataF datag, DataF navla, DataF navlo) {
  DataF datanc;
  datanc.fmin = datag.fmin;
  datanc.fmax = datag.fmax;

  // Define los extremos de acuerdo a las coordenadas originales
  int i, j=0;
  lamax = navla.fmax;
  lamin = navla.fmin;
  lomin = navlo.fmin;
  lomax = navlo.fmax;
  laheight = navla.height;
  lowidth = navlo.width;
  printf("%u %u - %g %g %g %g\n", laheight, lowidth, lamin, lamax, lomin, lomax);
  // Define las dimensiones del nuevo datanc
  datanc.width = datag.width;
  datanc.height = datag.height;
  datanc.size = datanc.width * datanc.height;

  datanc.data_in = malloc(sizeof(float) * datanc.size);

  double start = omp_get_wtime();
#pragma omp parallel for shared(datanc, datanc_big, factor)
  for (int j = 0; j < datag.height; j++)
    for (int i = 0; i < datag.width; i++) {
      unsigned ig = j * datag.width + i;
      unsigned is, js;
      getindices(navla.data_in[ig], navlo.data_in[ig], &is, &js);
      if (datag.data_in[ig] != NonData) 
        datanc.data_in[is] = dataf_value(datag, is, js);
      else
        datanc.data_in[is] = NonData;
    }
  // Interpola los datos faltantes
  // ...

  double end = omp_get_wtime();
  return datanc;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usanza %s <Archivo NetCDF ABI>\n", argv[0]);
    return -1;
  }
  DataNC dc;
  const char *fnc = argv[1];
  load_nc_sf(fnc, "Rad", &dc);

  // Obtén las coordenadas
  DataF navlo, navla, datagg;
  compute_navigation_nc(fnc, &navla, &navlo);

  datagg = geos2geographics(dc.base, navla, navlo);
  bool invert_values = false;
  bool use_alpha = false;
  ImageData imout = create_single_gray(datagg, invert_values, use_alpha);
  const char *outfn = "outlalo.png";
  write_image_png(outfn, &imout);

  return 0;
}
