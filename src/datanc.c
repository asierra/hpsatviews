/* NetCDF Data structure and tools
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>
#include <math.h>
#include <float.h>

#include "datanc.h"
#include "logger.h"

float NonData=1.0e+32;


// Constructor for DataF structure
DataF dataf_create(unsigned int width, unsigned int height) {
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

DataF dataf_copy(const DataF *data) {
    if (data == NULL || data->data_in == NULL || data->size == 0) {
        return dataf_create(0, 0); // Return an empty but valid DataF
    }

    // Create a new DataF struct on the stack
    DataF copy = dataf_create(data->width, data->height);
    if (copy.data_in == NULL && data->size > 0) {
        return copy; // Allocation failed, return empty struct
    }

    // Copy scalar members
    copy.fmin = data->fmin;
    copy.fmax = data->fmax;

    memcpy(copy.data_in, data->data_in, data->size * sizeof(float));

    return copy;
}

void dataf_fill(DataF *data, float value) {
    if (data == NULL || data->data_in == NULL || data->size == 0) {
		LOG_DEBUG("Trying to fill a NULL DataF.");
        return; // Nothing to fill
    }

    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        data->data_in[i] = value;
    }
}

DataF dataf_crop(const DataF *data, unsigned int x_start, unsigned int y_start, 
                 unsigned int width, unsigned int height) {
    if (data == NULL || data->data_in == NULL || data->size == 0) {
        return dataf_create(0, 0); // Return empty DataF
    }

    // Clamp crop region to source boundaries
    if (x_start >= data->width || y_start >= data->height) {
        return dataf_create(0, 0); // Start position is outside bounds
    }

    unsigned int effective_width = width;
    unsigned int effective_height = height;

    if (x_start + width > data->width) {
        effective_width = data->width - x_start;
    }
    if (y_start + height > data->height) {
        effective_height = data->height - y_start;
    }

    // Create new DataF for cropped region
    DataF cropped = dataf_create(effective_width, effective_height);
    if (cropped.data_in == NULL) {
        return cropped; // Allocation failed
    }

    // Copy data row by row
    #pragma omp parallel for
    for (unsigned int y = 0; y < effective_height; y++) {
        unsigned int src_y = y_start + y;
        size_t src_offset = src_y * data->width + x_start;
        size_t dst_offset = y * effective_width;
        memcpy(&cropped.data_in[dst_offset], &data->data_in[src_offset], 
               effective_width * sizeof(float));
    }

    // Recalculate min/max values for the cropped region
    float new_min = FLT_MAX;
    float new_max = -FLT_MAX;
    
    #pragma omp parallel for reduction(min:new_min) reduction(max:new_max)
    for (size_t i = 0; i < cropped.size; i++) {
        float val = cropped.data_in[i];
        if (val != NonData && !isnan(val)) {
            if (val < new_min) new_min = val;
            if (val > new_max) new_max = val;
        }
    }
    
    cropped.fmin = (new_min != FLT_MAX) ? new_min : data->fmin;
    cropped.fmax = (new_max != -FLT_MAX) ? new_max : data->fmax;

    return cropped;
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

  #pragma omp parallel for
  for (unsigned int j=0; j < datanc_big.height; j += factor)
    for (unsigned int i=0; i < datanc_big.width; i += factor) {
      int is = (j*datanc.width + i)/factor;
      datanc.data_in[is] = datanc_big.data_in[j*datanc_big.width + i];
    }
  double end = omp_get_wtime();
  printf("Tiempo downsampling simple %lf\n", end - start);
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

  #pragma omp parallel for
  for (unsigned int j=0; j < datanc.height; j++) {
    unsigned int ny = factor;
    unsigned int jj = j*factor;
    if (jj+ny > datanc_big.height)
      ny -= datanc_big.height - (jj + ny);
    for (unsigned int i=0; i < datanc.width; i++) {
      unsigned int nx = factor;
      unsigned int ii = i*factor;
      if (ii+nx > datanc_big.width)
        nx -= datanc_big.width - (ii + nx);
      unsigned int acum = 0;
      double f = 0;
      for (unsigned int l=0; l < ny; l++) {
        int jx = (jj+l)*datanc_big.width;
        for (unsigned int k=0; k < nx; k++) {
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

  #pragma omp parallel for
  for (unsigned int j=0; j < datanc.height; j++) {
    for (unsigned int i=0; i < datanc.width; i++) {
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

DataB datab_create(unsigned int width, unsigned int height) {
    DataB data;
    data.width = width;
    data.height = height;
    data.size = width * height;
    data.min = 0;
    data.max = 0;
    if (data.size > 0) {
        data.data_in = malloc(sizeof(int8_t) * data.size);
        if (data.data_in == NULL) {
            data.size = 0;
            data.width = 0;
            data.height = 0;
        }
    } else {
        data.data_in = NULL;
    }
    return data;
}

void datab_destroy(DataB *data) {
    if (data != NULL) {
        if (data->data_in != NULL) {
            free(data->data_in);
            data->data_in = NULL;
        }
        data->width = 0;
        data->height = 0;
        data->size = 0;
        data->min = 0;
        data->max = 0;
    }
}

void datanc_destroy(DataNC *datanc) {
    if (datanc) {
        if (datanc->is_float) {
            dataf_destroy(&datanc->fdata);
        } else {
            datab_destroy(&datanc->bdata);
        }
    }
}

DataF datanc_get_float_base(DataNC *datanc) {
    if (datanc && datanc->is_float) {
        return datanc->fdata;
    }
    // This case should be handled by the caller.
    // For now, return an empty struct if it's not float.
    // A better approach might be to convert byte to float here if needed.
    return dataf_create(0, 0);
}

DataF dataf_op_dataf(const DataF* a, const DataF* b, Operation op) {
    if (a->width != b->width || a->height != b->height) {
		LOG_ERROR("Dimensions of DataF operators must be the same.");
        return dataf_create(0, 0);
    }

    DataF result = dataf_create(a->width, a->height);
    if (result.data_in == NULL) return result;

    float fmin = 1e20f, fmax = -1e20f;

    #pragma omp parallel for reduction(min:fmin) reduction(max:fmax)
    for (size_t i = 0; i < a->size; i++) {
        float val_a = a->data_in[i];
        float val_b = b->data_in[i];

        if (val_a == NonData || val_b == NonData) {
            result.data_in[i] = NonData;
            continue;
        }

        float res_val;
        switch (op) {
            case OP_ADD: res_val = val_a + val_b; break;
            case OP_SUB: res_val = val_a - val_b; break;
            case OP_MUL: res_val = val_a * val_b; break;
            case OP_DIV:
                res_val = (fabsf(val_b) > 1e-9) ? (val_a / val_b) : NonData;
                break;
            default: res_val = NonData; break;
        }
        result.data_in[i] = res_val;

        if (res_val != NonData) {
            if (res_val < fmin) fmin = res_val;
            if (res_val > fmax) fmax = res_val;
        }
    }

    result.fmin = fmin;
    result.fmax = fmax;
    return result;
}

DataF dataf_op_scalar(const DataF* a, float scalar, Operation op, bool scalar_first) {
    DataF result = dataf_create(a->width, a->height);
    if (result.data_in == NULL) return result;

    float fmin = 1e20f, fmax = -1e20f;

    #pragma omp parallel for reduction(min:fmin) reduction(max:fmax)
    for (size_t i = 0; i < a->size; i++) {
        float val_a = a->data_in[i];
        if (val_a == NonData) {
            result.data_in[i] = NonData;
            continue;
        }

        float res_val;
        if (scalar_first) {
            switch (op) {
                case OP_ADD: res_val = scalar + val_a; break;
                case OP_SUB: res_val = scalar - val_a; break;
                case OP_MUL: res_val = scalar * val_a; break;
                case OP_DIV:
                    res_val = (fabsf(val_a) > 1e-9) ? (scalar / val_a) : NonData;
                    break;
                default: res_val = NonData; break;
            }
        } else {
            switch (op) {
                case OP_ADD: res_val = val_a + scalar; break;
                case OP_SUB: res_val = val_a - scalar; break;
                case OP_MUL: res_val = val_a * scalar; break;
                case OP_DIV:
                    res_val = (fabsf(scalar) > 1e-9) ? (val_a / scalar) : NonData;
                    break;
                default: res_val = NonData; break;
            }
        }
        result.data_in[i] = res_val;

        if (res_val != NonData) {
            if (res_val < fmin) fmin = res_val;
            if (res_val > fmax) fmax = res_val;
        }
    }

    result.fmin = fmin;
    result.fmax = fmax;
    return result;
}

void dataf_invert(DataF* a) {
    if (a == NULL || a->data_in == NULL) return;

    #pragma omp parallel for
    for (size_t i = 0; i < a->size; i++) {
        if (a->data_in[i] != NonData) {
            a->data_in[i] *= -1.0f;
        }
    }

    // Swap and invert min/max
    float old_fmin = a->fmin;
    a->fmin = -a->fmax;
    a->fmax = -old_fmin;
}


void dataf_apply_gamma(DataF *data, float gamma) {
    if (data == NULL || data->data_in == NULL || gamma <= 0.0f) {
        return;
    }

    // Un gamma de 1.0 no hace nada
    if (fabsf(gamma - 1.0f) < 1e-6) return;

    float inv_gamma = 1.0f / gamma;

    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        float val = data->data_in[i];
        
        // Ignorar NonData
        if (val == NonData) continue;

        // Protección contra valores negativos (común en correcciones atmosféricas agresivas)
        if (val < 0.0f) {
            data->data_in[i] = 0.0f;
        } else {
            // powf es la versión float de pow (más rápida)
            data->data_in[i] = powf(val, inv_gamma);
        }
    }

    // Actualizar min/max después de la transformación
    // Nota: Como la función es monotónica creciente, fmin y fmax 
    // simplemente se transforman igual, asumiendo que son positivos.
    if (data->fmin != NonData && data->fmin > 0) 
        data->fmin = powf(data->fmin, inv_gamma);
    else 
        data->fmin = 0.0f;
        
    if (data->fmax != NonData && data->fmax > 0) 
        data->fmax = powf(data->fmax, inv_gamma);
}
