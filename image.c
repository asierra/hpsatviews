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
#include "logger.h"
#include "datanc.h"


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

/**
 * @brief Crops an image to a specified rectangular region.
 * @param src Pointer to the source image data.
 * @param x The starting x-coordinate of the crop rectangle.
 * @param y The starting y-coordinate of the crop rectangle.
 * @param width The width of the crop rectangle.
 * @param height The height of the crop rectangle.
 * @return A new ImageData structure containing the cropped image.
 *         The caller is responsible for freeing this new image.
 *         Returns an empty image on failure.
 */
ImageData image_crop(const ImageData* src, unsigned int x, unsigned int y, unsigned int width, unsigned int height) {
    // Sanity checks
    if (src == NULL || src->data == NULL || width == 0 || height == 0) {
        return image_create(0, 0, 0);
    }
    if (x + width > src->width || y + height > src->height) {
        LOG_ERROR("El área de recorte excede las dimensiones de la imagen original.");
        return image_create(0, 0, 0);
    }

    ImageData cropped_img = image_create(width, height, src->bpp);
    if (cropped_img.data == NULL) {
        LOG_FATAL("Falla de memoria al crear la imagen recortada.");
        return cropped_img;
    }

    size_t src_row_stride = src->width * src->bpp;
    size_t cropped_row_stride = width * src->bpp;

    #pragma omp parallel for
    for (unsigned int i = 0; i < height; ++i) {
        // Pointer to the start of the source row
        const unsigned char* src_row = src->data + (y + i) * src_row_stride + x * src->bpp;
        // Pointer to the start of the destination row
        unsigned char* dst_row = cropped_img.data + i * cropped_row_stride;
        memcpy(dst_row, src_row, cropped_row_stride);
    }

    return cropped_img;
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

ImageData image_upsample_bilinear(const ImageData* src, int factor) {
    if (src == NULL || src->data == NULL || factor < 1) {
        return image_create(0, 0, 0);
    }
    
    unsigned int new_width = src->width * factor;
    unsigned int new_height = src->height * factor;
    ImageData result = image_create(new_width, new_height, src->bpp);
    
    if (result.data == NULL) {
        LOG_ERROR("No se pudo asignar memoria para el upsampling.");
        return result;
    }
    
    float xrat = (float)(src->width - 1) / (new_width - 1);
    float yrat = (float)(src->height - 1) / (new_height - 1);
    
    double start = omp_get_wtime();
    
    #pragma omp parallel for
    for (int j = 0; j < new_height; j++) {
        for (int i = 0; i < new_width; i++) {
            float x = xrat * i;
            float y = yrat * j;
            int xl = (int)floor(x);
            int yl = (int)floor(y);
            int xh = (int)ceil(x);
            int yh = (int)ceil(y);
            float xw = x - xl;
            float yw = y - yl;
            
            int dst_idx = (j * new_width + i) * src->bpp;
            
            for (int ch = 0; ch < src->bpp; ch++) {
                int idx_ll = (yl * src->width + xl) * src->bpp + ch;
                int idx_lh = (yl * src->width + xh) * src->bpp + ch;
                int idx_hl = (yh * src->width + xl) * src->bpp + ch;
                int idx_hh = (yh * src->width + xh) * src->bpp + ch;
                
                double val = src->data[idx_ll] * (1 - xw) * (1 - yw) +
                           src->data[idx_lh] * xw * (1 - yw) +
                           src->data[idx_hl] * (1 - xw) * yw +
                           src->data[idx_hh] * xw * yw;
                           
                result.data[dst_idx + ch] = (unsigned char)(val + 0.5);
            }
        }
    }
    
    double end = omp_get_wtime();
    LOG_INFO("Upsampling bilinear (factor=%d): %.3f segundos", factor, end - start);
    
    return result;
}

ImageData image_downsample_boxfilter(const ImageData* src, int factor) {
    if (src == NULL || src->data == NULL || factor < 1) {
        return image_create(0, 0, 0);
    }
    
    unsigned int new_width = src->width / factor;
    unsigned int new_height = src->height / factor;
    
    if (new_width == 0 || new_height == 0) {
        LOG_ERROR("El factor de downsampling es demasiado grande para esta imagen.");
        return image_create(0, 0, 0);
    }
    
    ImageData result = image_create(new_width, new_height, src->bpp);
    
    if (result.data == NULL) {
        LOG_ERROR("No se pudo asignar memoria para el downsampling.");
        return result;
    }
    
    double start = omp_get_wtime();
    
    #pragma omp parallel for
    for (int j = 0; j < new_height; j++) {
        for (int i = 0; i < new_width; i++) {
            int dst_idx = (j * new_width + i) * src->bpp;
            
            for (int ch = 0; ch < src->bpp; ch++) {
                double sum = 0.0;
                int count = 0;
                
                for (int dy = 0; dy < factor; dy++) {
                    for (int dx = 0; dx < factor; dx++) {
                        int src_x = i * factor + dx;
                        int src_y = j * factor + dy;
                        
                        if (src_x < src->width && src_y < src->height) {
                            int src_idx = (src_y * src->width + src_x) * src->bpp + ch;
                            sum += src->data[src_idx];
                            count++;
                        }
                    }
                }
                
                result.data[dst_idx + ch] = (unsigned char)((sum / count) + 0.5);
            }
        }
    }
    
    double end = omp_get_wtime();
    LOG_INFO("Downsampling box filter (factor=%d): %.3f segundos", factor, end - start);
    
    return result;
}

ImageData image_create_alpha_mask_from_dataf(const void* data_ptr) {
    const DataF* data = (const DataF*)data_ptr;
    if (data == NULL || data->data_in == NULL) {
        return image_create(0, 0, 0);
    }
    
    // Crear una imagen de 1 canal (grayscale) para la máscara
    ImageData mask = image_create(data->width, data->height, 1);
    if (mask.data == NULL) {
        LOG_ERROR("No se pudo crear máscara alpha.");
        return mask;
    }
    
    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        // 255 = opaco (dato válido), 0 = transparente (NonData)
        mask.data[i] = IS_NONDATA(data->data_in[i]) ? 0 : 255;
    }
    
    LOG_INFO("Máscara alpha creada: %ux%u", mask.width, mask.height);
    return mask;
}

ImageData image_add_alpha_channel(const ImageData* src, const ImageData* alpha_mask) {
    if (src == NULL || src->data == NULL || alpha_mask == NULL || alpha_mask->data == NULL) {
        return image_create(0, 0, 0);
    }
    
    if (src->width != alpha_mask->width || src->height != alpha_mask->height) {
        LOG_ERROR("Las dimensiones de la imagen y la máscara alpha no coinciden.");
        return image_create(0, 0, 0);
    }
    
    // Determinar nuevo bpp: 1->2 (gray+alpha), 3->4 (rgb+alpha)
    unsigned int new_bpp;
    if (src->bpp == 1) {
        new_bpp = 2;
    } else if (src->bpp == 3) {
        new_bpp = 4;
    } else {
        LOG_ERROR("Solo se puede agregar alpha a imágenes de 1 o 3 canales (bpp=%u).", src->bpp);
        return image_create(0, 0, 0);
    }
    
    ImageData result = image_create(src->width, src->height, new_bpp);
    if (result.data == NULL) {
        LOG_ERROR("No se pudo crear imagen con canal alpha.");
        return result;
    }
    
    size_t num_pixels = src->width * src->height;
    
    #pragma omp parallel for
    for (size_t i = 0; i < num_pixels; i++) {
        size_t src_idx = i * src->bpp;
        size_t dst_idx = i * new_bpp;
        
        // Copiar canales originales
        for (unsigned int ch = 0; ch < src->bpp; ch++) {
            result.data[dst_idx + ch] = src->data[src_idx + ch];
        }
        
        // Agregar canal alpha de la máscara
        result.data[dst_idx + src->bpp] = alpha_mask->data[i];
    }
    
    LOG_INFO("Canal alpha agregado: %ux%u, bpp %u->%u", result.width, result.height, src->bpp, new_bpp);
    return result;
}
