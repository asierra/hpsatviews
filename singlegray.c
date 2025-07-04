/* Creates a single BW image from an original NC.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "args.h"
#include "reader_nc.h"
#include "writer_png.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

ImageData create_single_gray(DataNC c01, bool invert_value, bool use_alpha) {
  ImageData imout;
  imout.bpp = (use_alpha) ? 2: 1;
  imout.width = c01.base.width;
  imout.height = c01.base.height;
  imout.data = malloc(imout.bpp * c01.base.size);

  double start = omp_get_wtime();

#pragma omp parallel for shared(c01, imout)
  float dd = c01.base.fmax - c01.base.fmin;
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;
      unsigned char r = 0, a = 0;
      if (c01.base.data_in[i] < NonData) {
        float f;
        if (invert_value)
          f = (c01.base.fmax - c01.base.data_in[i]) / dd;
        else
          f = (c01.base.data_in[i] - c01.base.fmin) / dd;
        r = (unsigned char)(255.0 * f);
        a = 255;
      }
      imout.data[po] = r;
      if (imout.bpp == 2)
        imout.data[po + 1] = a;
    }
  }

  double end = omp_get_wtime();
  printf("Tiempo Single Gray %lf\n", end - start);
  return imout;
}

int main(int argc, char *argv[]) {
  char *fnc01, *outfn;
  bool invert_values = false;
  bool apply_histogram = false;
  bool use_alpha = false;
  float gamma = 0;
  int scale = 1;

  ArgParser *parser = ap_new_parser();
  ap_set_helptext(parser,
                  "Usanza: singlegray [-i (invertir)] [-h (usar histograma)] "
                  "[-g gamma] [-s scale] <Archivo NetCDF ABI L1b>");
  ap_set_version(parser, "1.0");
  ap_add_str_opt(parser, "out o", "out.png");
  ap_add_dbl_opt(parser, "gamma g", 1);
  ap_add_flag(parser, "histo h");
  ap_add_flag(parser, "invert i");
  ap_add_flag(parser, "alpha a");
  ap_add_int_opt(parser, "scale s", 1);

  if (!ap_parse(parser, argc, argv)) {
    exit(1);
  }

  if (ap_has_args(parser))
    fnc01 = ap_get_arg_at_index(parser, 0);
  else {
    printf("Error: Debes dar un nombre de archivo NetCDF con datos GOES ABI L1b.\n");
    return -1;
  }
  outfn = ap_get_str_value(parser, "out");
  invert_values = ap_found(parser, "invert");
  apply_histogram = ap_found(parser, "histo");
  use_alpha = ap_found(parser, "alpha");
  if (ap_found(parser, "gamma"))
    gamma = ap_get_dbl_value(parser, "gamma");
  if (ap_found(parser, "scale"))
    scale = ap_get_int_value(parser, "scale");
    printf("escala %d\n", scale);
  ap_free(parser);

  DataNC c01;
  load_nc_sf(fnc01, "Rad", &c01);
  if (scale < 0) {
    DataF aux = downsample_boxfilter(c01.base, -scale);
    free(c01.base.data_in);
    c01.base = aux;
  } else if (scale > 1) {
    DataF aux = upsample_bilinear(c01.base, scale);
    free(c01.base.data_in);
    c01.base = aux;
  }
  ImageData imout = create_single_gray(c01, invert_values, use_alpha);
  if (gamma != 0) 
    image_apply_gamma(imout, gamma);
  if (apply_histogram)
    image_apply_histogram(imout);
  write_image_png(outfn, &imout);

  return 0;
}