#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_nc.h"
#include "writer_png.h"

char id[13] = "sAAAAJJJHHmm";
char *fnc01, *fnc02, *fnc03, *fnc13;

ImageData create_truecolor_composite(DataNC B, DataNC R, DataNC NiR);

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

int find_channel_filenames(const char *dirnm) {
  struct dirent **namelist;
  int n;
  n = scandir(dirnm, &namelist, filter, alphasort);
  printf("%d\n", n);
  if (n != 16)
    return -1;
  fnc01 = namelist[0]->d_name;
  fnc02 = namelist[1]->d_name;
  fnc03 = namelist[2]->d_name;
  fnc13 = namelist[12]->d_name;
  return 0;
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
    printf("Usanza %s <Archivo NetCDF ABI L2>\n", argv[0]);
    return -1;
  }
  const char *basenm = basename(argv[1]);
  const char *dirnm = dirname(argv[1]);

  find_id_from_name(basenm);
  find_channel_filenames(dirnm);

  DataNC c01, c02, c03, c13, aux;
  load_nc_sf(fnc01, &c01, "Rad", 1);
  load_nc_sf(fnc02, &c02, "Rad", 1);
  load_nc_sf(fnc03, &c03, "Rad", 1);

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

  ImageData diurna = create_truecolor_composite(c01, c02, c03);
  //ImageData nocturna = create_nocturnal_composite(c13);

  write_image_png("nite.png", &diurna);

  return 0;
}
