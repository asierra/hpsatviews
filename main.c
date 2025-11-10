/* Main program where everything is built and it's decided which
 * images will be stored.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "reader_nc.h"
#include "writer_png.h"
#include "image.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structure to hold channel information
typedef struct {
    const char* name;    // Channel name (e.g., "C01")
    char* filename;      // Full path to file (allocated)
} ChannelInfo;

// Structure to hold all required channels for processing
typedef struct {
    ChannelInfo* channels;
    int count;
    char* id_signature;  // Copy of the ID signature
} ChannelSet;

// Create a ChannelSet with specified channel names
ChannelSet* channelset_create(const char* channel_names[], int count) {
    ChannelSet* set = malloc(sizeof(ChannelSet));
    if (!set) return NULL;
    
    set->channels = malloc(sizeof(ChannelInfo) * count);
    if (!set->channels) {
        free(set);
        return NULL;
    }
    
    set->count = count;
    set->id_signature = malloc(13);  // Size of id array
    if (!set->id_signature) {
        free(set->channels);
        free(set);
        return NULL;
    }
    
    // Initialize channels
    for (int i = 0; i < count; i++) {
        set->channels[i].name = channel_names[i];
        set->channels[i].filename = NULL;
    }
    
    return set;
}

// Free a ChannelSet and all its allocated memory
void channelset_destroy(ChannelSet* set) {
    if (!set) return;
    
    if (set->channels) {
        for (int i = 0; i < set->count; i++) {
            free(set->channels[i].filename);
        }
        free(set->channels);
    }
    
    free(set->id_signature);
    free(set);
}

// Find a specific channel by name in the set
ChannelInfo* channelset_find(ChannelSet* set, const char* channel_name) {
    if (!set || !channel_name) return NULL;
    
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->channels[i].name, channel_name) == 0) {
            return &set->channels[i];
        }
    }
    return NULL;
}


ImageData create_truecolor_rgb(DataF B, DataF R, DataF NiR);

ImageData create_nocturnal_pseudocolor(DataNC datanc);

ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo,
                               float *dnratio, float max_tmp);


int find_id_from_name(const char *name, char *id_buffer) {
  int pos = 0;

  while (name[pos] != 's' && name[pos] > 32)
    pos++;

  if (name[pos] <= 32)
    return -1;

  for (int i = 1; i < 12; i++)
    id_buffer[i] = name[pos + i];

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

int find_channel_filenames(const char *dirnm, ChannelSet* channelset) {
  DIR *d;
  struct dirent *dir;
  d = opendir(dirnm);
  if (d == NULL) {
    return -1;
  }

  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, channelset->id_signature) != NULL && 
        strstr(dir->d_name, ".nc") != NULL) {
      
      // Check each channel in the set
      for (int i = 0; i < channelset->count; i++) {
        if (strstr(dir->d_name, channelset->channels[i].name) != NULL) {
          // Free existing filename if any
          free(channelset->channels[i].filename);
          // Allocate new filename
          channelset->channels[i].filename = concat(dirnm, dir->d_name);
          break;
        }
      }
    }
  }
  closedir(d);
  return 0;
}

int truecolornight(const char* input_file) {
  const char *basenm = basename((char*)input_file);
  const char *dirnm = dirname((char*)input_file);

  // Create channel set for required bands
  const char* required_channels[] = {"C01", "C02", "C03", "C13"};
  ChannelSet* channels = channelset_create(required_channels, 4);
  if (!channels) {
    printf("Error: Failed to create channel set\n");
    return -1;
  }

  // Extract ID from filename
  strcpy(channels->id_signature, "sAAAAJJJHHmm");  // Initialize with default
  if (find_id_from_name(basenm, channels->id_signature) != 0) {
    printf("Error: Could not extract ID from filename\n");
    channelset_destroy(channels);
    return -1;
  }

  // Find all channel files
  if (find_channel_filenames(dirnm, channels) != 0) {
    printf("Error: Could not access directory %s\n", dirnm);
    channelset_destroy(channels);
    return -1;
  }

  // Verify all required files are found
  for (int i = 0; i < channels->count; i++) {
    if (channels->channels[i].filename == NULL) {
      printf("Error: Missing file for channel %s\n", channels->channels[i].name);
      channelset_destroy(channels);
      return -1;
    }
  }

  // Get individual channel filenames for easier access
  ChannelInfo* c01_info = channelset_find(channels, "C01");
  ChannelInfo* c02_info = channelset_find(channels, "C02");
  ChannelInfo* c03_info = channelset_find(channels, "C03");
  ChannelInfo* c13_info = channelset_find(channels, "C13");

  // Load NetCDF data
  DataNC c01, c02, c03, c13;
  DataF aux, navlo, navla;
  
  load_nc_sf(c01_info->filename, "Rad", &c01);
  load_nc_sf(c02_info->filename, "Rad", &c02);
  load_nc_sf(c03_info->filename, "Rad", &c03);
  load_nc_sf(c13_info->filename, "Rad", &c13);

  char downsample = 1;

  if (downsample) {
    // Iguala los tamaños a la resolución mínima
    aux = downsample_boxfilter(c01.base, 2);
    dataf_destroy(&c01.base);
    c01.base = aux;
    aux = downsample_boxfilter(c02.base, 4);
    dataf_destroy(&c02.base);
    c02.base = aux;
    aux = downsample_boxfilter(c03.base, 2);
    dataf_destroy(&c03.base);
    c03.base = aux;
    compute_navigation_nc(c13_info->filename, &navla, &navlo);
  } else {
    // Iguala los tamaños a la resolución máxima
    aux = upsample_bilinear(c01.base, 2);
    dataf_destroy(&c01.base);
    c01.base = aux;
    aux = upsample_bilinear(c13.base, 4);
    dataf_destroy(&c13.base);
    c13.base = aux;
    aux = upsample_bilinear(c03.base, 2);
    dataf_destroy(&c03.base);
    c03.base = aux;
    compute_navigation_nc(c02_info->filename, &navla, &navlo);
  }
  
  // Create images
  ImageData diurna = create_truecolor_rgb(c01.base, c02.base, c03.base);
  image_apply_histogram(diurna);
  ImageData nocturna = create_nocturnal_pseudocolor(c13);

  float dnratio;
  ImageData mask = create_daynight_mask(c13, navla, navlo, &dnratio, 263.15);
  printf("daynight ratio %g\n", dnratio);

  // Save images
  write_image_png("dia.png", &diurna);
  write_image_png("noche.png", &nocturna);
  write_image_png("mask.png", &mask);

  if (dnratio < 0.15)
    write_image_png("out.png", &nocturna);
  else {
    ImageData blend = blend_images(nocturna, diurna, mask);
    write_image_png("out.png", &blend);
    image_destroy(&blend);
  }
  
  // Free all memory
  dataf_destroy(&c01.base);
  dataf_destroy(&c02.base);
  dataf_destroy(&c03.base);
  dataf_destroy(&c13.base);
  dataf_destroy(&navla);
  dataf_destroy(&navlo);
  
  image_destroy(&diurna);
  image_destroy(&nocturna);
  image_destroy(&mask);
  
  // Free channel set (includes all filenames)
  channelset_destroy(channels);
  
  return 0;
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
    printf("Usanza %s <Archivo NetCDF ABI L1b>\n", argv[0]);
    return -1;
  }

  return truecolornight(argv[1]);
}
