/* Creates a dataset in geographics from a geostationary dataset.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "reader_nc.h"
#include "writer_png.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>

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
  int i, j;
  lamax = navla.data_in[navla.width / 2];
  lamin = navla.data_in[(navla.height - 1) * navla.width + navla.width / 2];
  lomin = navlo.data_in[(navlo.height / 2) * navlo.width];
  lomax = navlo.data_in[(navlo.height / 2) * navlo.width + navlo.width - 1];
  laheight = navla.height;
  lowidth = navlo.width;

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
      if (datag.data_in[ig] != NonData) {
        unsigned is, js;
        getindices(navla.data_in[ig], navlo.data_in[ig], &is, &js);
        is = (js * datanc.width + is);
        datanc.data_in[is] = datag.data_in[ig];
      }
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
  char *outfn;
  write_image_png(outfn, &imout);

  return 0;
}
