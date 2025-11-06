/* NetCDF Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include <math.h>

#include "datanc.h"

float NonData=1.0e+32;


// Constructor for DataF structure
DataF dataf_create(size_t width, size_t height) {
    DataF data;
    
    // Initialize all fields
    data.width = width;
    data.height = height;
    data.size = width * height;
    data.fmin = 0.0f;
    data.fmax = 0.0f;
    
    // Allocate memory with error checking
    if (data.size > 0) {
        data.data_in = malloc(sizeof(float) * data.size);
        if (data.data_in == NULL) {
            // On allocation failure, set data_in to NULL and size to 0
            data.data_in = NULL;
            data.size = 0;
            data.width = 0;
            data.height = 0;
        }
    } else {
        data.data_in = NULL;
    }
    
    return data;
}

// Destructor for DataF structure
void dataf_destroy(DataF *data) {
    if (data != NULL) {
        if (data->data_in != NULL) {
            free(data->data_in);
            data->data_in = NULL;
        }
        // Reset all fields to safe values
        data->width = 0;
        data->height = 0;
        data->size = 0;
        data->fmin = 0.0f;
        data->fmax = 0.0f;
    }
}


DataF downsample_simple(DataF datanc_big, int factor)
{
  DataF datanc = dataf_create(datanc_big.width/factor, datanc_big.height/factor);
  
  // Check if allocation was successful
  if (datanc.data_in == NULL) {
    return datanc; // Return empty DataF on allocation failure
  }
  
  datanc.fmin = datanc_big.fmin;
  datanc.fmax = datanc_big.fmax;
  
  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datanc_big.height; j += factor)
    for (int i=0; i < datanc_big.width; i += factor) {
      int is = (j*datanc.width + i)/factor;
      datanc.data_in[is] = datanc_big.data_in[j*datanc_big.width + i];
    }
  double end = omp_get_wtime();
  return datanc;
}


DataF downsample_boxfilter(DataF datanc_big, int factor)
{
  DataF datanc = dataf_create(datanc_big.width/factor, datanc_big.height/factor);
  
  // Check if allocation was successful
  if (datanc.data_in == NULL) {
    return datanc; // Return empty DataF on allocation failure
  }
  
  datanc.fmin = datanc_big.fmin;
  datanc.fmax = datanc_big.fmax;
  
  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datanc.height; j++) {
    int ny = factor;
    int jj = j*factor;
    if (jj+ny > datanc_big.height)
      ny -= datanc_big.height - (jj + ny);
    for (int i=0; i < datanc.width; i++) {
      int nx = factor;
      int ii = i*factor;
      if (ii+nx > datanc_big.width)
        nx -= datanc_big.width - (ii + nx);
      int acum = 0;
      double f = 0;
      for (int l=0; l < ny; l++) {
        int jx = (jj+l)*datanc_big.width;
        for (int k=0; k < nx; k++) {
          f += datanc_big.data_in[jx + ii + k];
          acum++;
        }
      }
      datanc.data_in[j*datanc.width + i] = (float)(f/acum);
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo downsampling boxfilter %lf\n", end - start);

  return datanc;
}


DataF upsample_bilinear(DataF datanc_small, int factor)
{
  DataF datanc = dataf_create(datanc_small.width*factor, datanc_small.height*factor);
  
  // Check if allocation was successful
  if (datanc.data_in == NULL) {
    return datanc; // Return empty DataF on allocation failure
  }
  
  datanc.fmin = datanc_small.fmin;
  datanc.fmax = datanc_small.fmax;
  
  float xrat = (float)(datanc_small.width - 1)/(datanc.width - 1);
  float yrat = (float)(datanc_small.height - 1)/(datanc.height - 1);

  double start = omp_get_wtime();

  #pragma omp parallel for shared(datanc, datanc_small, factor) 
  for (int j=0; j < datanc.height; j++) {
    for (int i=0; i < datanc.width; i++) {
      float x = xrat * i;
      float y = yrat * j;
      int xl = (int)floor(x);
      int yl = (int)floor(y);
      int xh = (int)ceil(x);
      int yh = (int)ceil(y);
      float xw = x - xl;
      float yw = y - yl;

      double d = datanc_small.data_in[yl*datanc_small.width + xl]*(1 -xw)*(1 - yw) +
        datanc_small.data_in[yl*datanc_small.width + xh]*xw*(1 - yw) + 
        datanc_small.data_in[yh*datanc_small.width + xl]*(1 -xw)*yw +
        datanc_small.data_in[yh*datanc_small.width + xh]*xw*yw;
      datanc.data_in[j*datanc.width + i] = (float)d;
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo upsampling bilinear %lf\n", end - start);

  return datanc;
}

