/* NetCDF Data structure and tools
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>
#include <math.h>

#include "datanc.h"

float NonData=1.0e+32;


// Constructor for DataF structure
DataF dataf_create(size_t width, size_t height, DataType t) {
    DataF data;
    
    // Initialize all fields
    data.width = width;
    data.height = height;
    data.size = width * height;
    data.fmin = 0.0f;
    data.fmax = 0.0f;
    data.type = t;
    // Allocate memory with error checking
    if (data.size > 0) {
        if (t==DATA_TYPE_FLOAT)
          data.data_in = malloc(sizeof(float) * data.size);
        else if (t==DATA_TYPE_INT8)
          data.data_in = malloc(sizeof(int8_t) * data.size);
        if (data.data_in==NULL) {
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

/**
 * @brief Creates a deep copy of a DataF structure.
 * 
 * This function allocates new memory for both the structure and its data buffer,
 * then copies the content from the source. The caller is responsible for freeing
 * the returned pointer using dataf_destroy().
 * 
 * @param data A pointer to the constant DataF structure to be copied.
 * @return A pointer to the newly created DataF copy, or NULL if memory
 *         allocation fails or the source is NULL.
 */
DataF dataf_copy(const DataF *data) {
    if (data == NULL || data->data_in == NULL) {
        return dataf_create(0, 0, DATA_TYPE_FLOAT); // Return an empty but valid DataF
    }

    // Create a new DataF struct on the stack
    DataF copy = dataf_create(data->width, data->height, data->type);
    if (copy.data_in == NULL && data->size > 0) {
        return copy; // Allocation failed, return empty struct
    }

    // Copy scalar members
    copy.fmin = data->fmin;
    copy.fmax = data->fmax;
    copy.type = data->type;

    // Copia el buffer de datos, manejando los diferentes tipos de datos.
    size_t bytes_to_copy;
    if (data->type == DATA_TYPE_FLOAT) {
        bytes_to_copy = data->size * sizeof(float);
    } else { // DATA_TYPE_INT8
        bytes_to_copy = data->size * sizeof(int8_t);
    }
    memcpy(copy.data_in, data->data_in, bytes_to_copy);

    return copy;
}

/**
 * @brief Fills the data buffer of a DataF structure with a specific value.
 * 
 * This function iterates through the entire data_in buffer and sets each
 * element to the provided floating-point value. It is parallelized with OpenMP
 * for efficiency on large datasets.
 * 
 * @param data A pointer to the DataF structure to be filled.
 * @param value The float value to fill the buffer with.
 */
void dataf_fill(DataF *data, float value) {
    if (data == NULL || data->data_in == NULL || data->size == 0) {
        return; // Nothing to fill
    }

    float* data_data_in = data->data_in;
    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        data_data_in[i] = value;
    }
}

DataF downsample_simple(DataF datanc_big, int factor)
{
  DataF datanc = dataf_create(datanc_big.width/factor, datanc_big.height/factor, datanc_big.type);
  
  // Check if allocation was successful
  if (datanc.data_in == NULL) {
    return datanc; // Return empty DataF on allocation failure
  }
  
  datanc.fmin = datanc_big.fmin;
  datanc.fmax = datanc_big.fmax;
  
  double start = omp_get_wtime();
  float *datanc_data_in = (float*)datanc.data_in;
  float *datanc_big_data_in = (float*)datanc_big.data_in;

  #pragma omp parallel for shared(datanc, datanc_big, factor, datanc_data_in, datanc_big_data_in) 
  for (int j=0; j < datanc_big.height; j += factor)
    for (int i=0; i < datanc_big.width; i += factor) {
      int is = (j*datanc.width + i)/factor;
      datanc_data_in[is] = datanc_big_data_in[j*datanc_big.width + i];
    }
  double end = omp_get_wtime();
  printf("Tiempo downsampling simple %lf\n", end - start);
  return datanc;
}


DataF downsample_boxfilter(DataF datanc_big, int factor)
{
  DataF datanc = dataf_create(datanc_big.width/factor, datanc_big.height/factor, datanc_big.type);
  
  // Check if allocation was successful
  if (datanc.data_in == NULL) {
    return datanc; // Return empty DataF on allocation failure
  }
  
  datanc.fmin = datanc_big.fmin;
  datanc.fmax = datanc_big.fmax;
  
  double start = omp_get_wtime();
  float *datanc_data_in = (float*)datanc.data_in;
  float *datanc_big_data_in = (float*)datanc_big.data_in;

  #pragma omp parallel for shared(datanc, datanc_big, factor, datanc_data_in, datanc_big_data_in) 
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
          f += datanc_big_data_in[jx + ii + k];
          acum++;
        }
      }
      datanc_data_in[j*datanc.width + i] = (float)(f/acum);
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo downsampling boxfilter %lf\n", end - start);

  return datanc;
}


DataF upsample_bilinear(DataF datanc_small, int factor)
{
  DataF datanc = dataf_create(datanc_small.width*factor, datanc_small.height*factor, datanc_small.type);
  
  // Check if allocation was successful
  if (datanc.data_in == NULL) {
    return datanc; // Return empty DataF on allocation failure
  }
  
  datanc.fmin = datanc_small.fmin;
  datanc.fmax = datanc_small.fmax;
  
  float xrat = (float)(datanc_small.width - 1)/(datanc.width - 1);
  float yrat = (float)(datanc_small.height - 1)/(datanc.height - 1);

  double start = omp_get_wtime();
  float *datanc_data_in = (float*)datanc.data_in;
  float *datanc_small_data_in = (float*)datanc_small.data_in;

  #pragma omp parallel for shared(datanc, datanc_small, factor, datanc_data_in, datanc_small_data_in) 
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

      double d = datanc_small_data_in[yl*datanc_small.width + xl]*(1 -xw)*(1 - yw) +
        datanc_small_data_in[yl*datanc_small.width + xh]*xw*(1 - yw) + 
        datanc_small_data_in[yh*datanc_small.width + xl]*(1 -xw)*yw +
        datanc_small_data_in[yh*datanc_small.width + xh]*xw*yw;
      datanc_data_in[j*datanc.width + i] = (float)d;
    }
  }
  double end = omp_get_wtime();
  printf("Tiempo upsampling bilinear %lf\n", end - start);

  return datanc;
}
