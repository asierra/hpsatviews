#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_nc.h"
#include "writer_png.h"

char id[13] = "sAAAAJJJHHmm";
char *fnc01, *fnc02, *fnc03, *fnc13;

ImageData create_truecolor_composite(DataNC B, DataNC R, DataNC NiR,
                                     unsigned char hgram);

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

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 2); 
    strcpy(result, s1);
    result[strlen(s1)] = '/';
    result[strlen(s1)+1] = 0;
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
    printf("name %d : %s\n", i, namelist[i]->d_name);
    if (strstr(namelist[i]->d_name, "C01") != NULL)
      fnc01 = concat(dirnm, namelist[i]->d_name);
    if (strstr(namelist[i]->d_name, "C02") != NULL)
      fnc02 = concat(dirnm, namelist[i]->d_name);
    if (strstr(namelist[i]->d_name, "C03") != NULL)
      fnc03 = concat(dirnm, namelist[i]->d_name);
    //if (strstr(namelist[i]->d_name, "C13") != NULL)
    //  fnc13 = concat(dirnm, namelist[i]->d_name);
  }
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
  printf("Files %s %s %s\n", fnc01, fnc02, fnc03);
  
  DataNC c01, c02, c03, c13, aux;
  load_nc_sf(fnc01, &c01, "CMI", 0);
  load_nc_sf(fnc02, &c02, "CMI", 0);
  load_nc_sf(fnc03, &c03, "CMI", 0);

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

  ImageData diurna = create_truecolor_composite(c01, c02, c03, 1);
  //ImageData nocturna = create_nocturnal_composite(c13);

  write_image_png("out.png", &diurna);

  return 0;
}
