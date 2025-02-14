/* Main program where everything is built and it's decided which
 * images will be stored.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "reader_nc.h"
#include "writer_png.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Date and time signature
char id[13] = "sAAAAJJJHHmm";

// Input filenames full path
char *fnc01, *fnc02, *fnc03, *fnc13;

ImageData create_truecolor_rgb(DataNC B, DataNC R, DataNC NiR,
                               unsigned char hgram);

ImageData create_nocturnal_pseudocolor(DataNC datanc);

ImageData create_daynight_mask(DataNC datanc, DataNCF navla, DataNCF navlo,
                               float *dnratio, float max_tmp);


int find_id_from_name(const char *name) {
  int pos = 0;
  int len = strlen(name);

  while (name[pos] != 's' && name[pos] > 32)
    pos++;

  if (name[pos] <= 32)
    return -1;

  for (int i = 1; i < 12; i++)
    id[i] = name[pos + i];

  return 0;
}

int filter(const struct dirent *entry) {
  if (strstr(entry->d_name, id) != NULL)
    return -1;
  else
    return 0;
}

char *concat(const char *s1, const char *s2) {
  char *result = malloc(strlen(s1) + strlen(s2) + 2);
  strcpy(result, s1);
  result[strlen(s1)] = '/';
  result[strlen(s1) + 1] = 0;
  strcat(result, s2);
  return result;
}

int find_channel_filenames(const char *dirnm) {
  struct dirent **namelist;
  int n;
  n = scandir(dirnm, &namelist, filter, alphasort);

  if (n < 2)
    return -1;

  for (int i = 0; i < n; i++) {
    if (strstr(namelist[i]->d_name, ".nc") != NULL) {
      if (strstr(namelist[i]->d_name, "C01") != NULL)
        fnc01 = concat(dirnm, namelist[i]->d_name);
      else if (strstr(namelist[i]->d_name, "C02") != NULL)
        fnc02 = concat(dirnm, namelist[i]->d_name);
      else if (strstr(namelist[i]->d_name, "C03") != NULL)
        fnc03 = concat(dirnm, namelist[i]->d_name);
      else if (strstr(namelist[i]->d_name, "C13") != NULL)
        fnc13 = concat(dirnm, namelist[i]->d_name);
    }
  }
  return 0;
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
    printf("Usanza %s <Archivo NetCDF ABI L1b>\n", argv[0]);
    return -1;
  }
  const char *basenm = basename(argv[1]);
  const char *dirnm = dirname(argv[1]);

  find_id_from_name(basenm);

  fnc01 = fnc02 = fnc03 = fnc13 = NULL;
  find_channel_filenames(dirnm);

  // Verifica que existen todos los archivos necesarios y si no,  marca error
  if (fnc01 == NULL || fnc02 == NULL || fnc03 == NULL || fnc13 == NULL) {
    printf("Error: Faltan archivos para poder constriur la imagen final.\n");
    return -1;
  }
  // printf("Files %s %s %s %s %s\n", fnc01, fnc02, fnc03, fnc13, fnnav);

  DataNC c01, c02, c03, c13, aux;
  DataNCF navlo, navla;

  compute_navigation_nc(fnc13, &navla, &navlo);
  
  load_nc_sf(fnc01, "Rad", &c01);
  load_nc_sf(fnc02, "Rad", &c02);
  load_nc_sf(fnc03, "Rad", &c03);
  load_nc_sf(fnc13, "Rad", &c13);

  // Iguala los tamaños a la resolución mínima
  aux = downsample_neighbor_nc(c01, 2);
  free(c01.data_in);
  c01 = aux;
  aux = downsample_neighbor_nc(c02, 4);
  free(c02.data_in);
  c02 = aux;
  aux = downsample_neighbor_nc(c03, 2);
  free(c03.data_in);
  c03 = aux;

  ImageData diurna = create_truecolor_rgb(c01, c02, c03, 1);
  ImageData nocturna = create_nocturnal_pseudocolor(c13);

  float dnratio;
  ImageData mask = create_daynight_mask(c13, navla, navlo, &dnratio, 263.15);
  printf("daynight ratio %g\n", dnratio);

  write_image_png("dia.png", &diurna);
  write_image_png("noche.png", &nocturna);
  write_image_png("mask.png", &mask);

  if (dnratio < 0.15)
    write_image_png("out.png", &nocturna);
  else {
    ImageData blend = blend_images(nocturna, diurna, mask);
    write_image_png("out.png", &blend);
  }
  // Free all memory
  free(c01.data_in);
  free(c02.data_in);
  free(c03.data_in);
  free(c13.data_in);
  free(navla.data_in);
  free(navlo.data_in);
  free(fnc01);
  free(fnc02);
  free(fnc03);
  free(fnc13);
  
  return 0;
}
