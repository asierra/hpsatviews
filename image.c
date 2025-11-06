/* Image data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"


// Constructor for ImageData structure
ImageData image_create(unsigned int width, unsigned int height, unsigned int bpp) {
    ImageData image;
    
    // Initialize all fields
    image.width = width;
    image.height = height;
    image.bpp = bpp;
    
    // Validate bpp parameter
    if (bpp < 1 || bpp > 4) {
        image.data = NULL;
        image.width = 0;
        image.height = 0;
        image.bpp = 0;
        return image;
    }
    
    // Calculate total size needed
    size_t total_size = (size_t)width * height * bpp;
    
    // Allocate memory with error checking
    if (total_size > 0) {
        image.data = malloc(total_size);
        if (image.data == NULL) {
            // On allocation failure, reset all fields
            image.width = 0;
            image.height = 0;
            image.bpp = 0;
        }
    } else {
        image.data = NULL;
    }
    
    return image;
}

// Destructor for ImageData structure
void image_destroy(ImageData *image) {
    if (image != NULL) {
        if (image->data != NULL) {
            free(image->data);
            image->data = NULL;
        }
        // Reset all fields to safe values
        image->width = 0;
        image->height = 0;
        image->bpp = 0;
    }
}


ImageData copy_image(ImageData orig) {
  size_t size = orig.width * orig.height;
  ImageData imout = image_create(orig.width, orig.height, orig.bpp);
  
  // Check if allocation was successful
  if (imout.data != NULL && orig.data != NULL) {
    memcpy(imout.data, orig.data, size * orig.bpp);
  }
  
  return imout;
}


ImageData blend_images(ImageData bg, ImageData fg, ImageData mask) {
  size_t size = bg.width * bg.height;
  ImageData imout = image_create(bg.width, bg.height, bg.bpp);
  
  // Check if allocation was successful
  if (imout.data == NULL) {
    return imout; // Return empty image on allocation failure
  }

  double start = omp_get_wtime();

#pragma omp parallel for shared(bg, fg, mask, imout)
  for (int i = 0; i < size; i++) {
    int p = i * bg.bpp;
    int pm = i * mask.bpp;

    float w = (float)(mask.data[pm] / 255.0);

    imout.data[p] = (unsigned char)(w * bg.data[p] + (1 - w) * fg.data[p]);
    imout.data[p + 1] =
        (unsigned char)(w * bg.data[p + 1] + (1 - w) * fg.data[p + 1]);
    imout.data[p + 2] =
        (unsigned char)(w * bg.data[p + 2] + (1 - w) * fg.data[p + 2]);
  }
  double end = omp_get_wtime();
  printf("Tiempo blend %lf\n", end - start);

  return imout;
}


void image_apply_histogram(ImageData im) {
  size_t size = im.width * im.height;
  unsigned int histogram[255];

  for (int i = 0; i < 255; i++)
    histogram[i] = 0;

  for (int y = 0; y < im.height; y++) {
    for (int x = 0; x < im.width; x++) {
      int i = y * im.width + x;
      int po = i * im.bpp;
      unsigned int q;
      if (im.bpp >= 3) 
        // Average luminosity
        q = (unsigned int)((im.data[po] + im.data[po+1] + 
            im.data[po+2] + 0.5) / 3.0);
      else 
        q = im.data[po];
      histogram[q]++;
    }
  }

  unsigned int cum = 0;
  unsigned char transfer[255]; // Función de transferencia
  for (int i = 0; i < 256; i++) {
    cum += histogram[i];
    transfer[i] = (unsigned char)(255.0 * cum / size);
  }
  for (int i = 0; i < size; i++) {
      int p = i*im.bpp;
      im.data[p] = transfer[im.data[p]];
      if (im.bpp >= 3) {
        im.data[p + 1] = transfer[im.data[p + 1]];
        im.data[p + 2] = transfer[im.data[p + 2]];
      }
  }
}


void image_apply_gamma(ImageData im, float gamma) {
  size_t size = im.width * im.height;
  unsigned char nvalues[256];

  for (int i = 0; i < 256; i++)
    nvalues[i] = (unsigned char)(255 * pow(i / 255.0, gamma));

  for (int i = 0; i < size; i++) {
    int p = i * im.bpp;
    int j = im.data[p];
    im.data[p] = nvalues[j];
    if (im.bpp >= 3) {
        im.data[p + 1] = nvalues[im.data[p + 1]];
        im.data[p + 2] = nvalues[im.data[p + 2]];
      }
  }
}
