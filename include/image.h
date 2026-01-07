#ifndef HPSATVIEWS_IMAGE_H_
#define HPSATVIEWS_IMAGE_H_

#include <stdint.h>
#include <stdlib.h>

// Estructura para guardar datos de una imagen
typedef struct {
  unsigned int width, height;
  unsigned int bpp; // Bytes per pixel: 1 = gray, 2 = gray+a, 3 = rgb, 4 = rgba
  unsigned char *data;
} ImageData;

typedef struct {
    uint8_t r, g, b;
} Color;

typedef struct {
    unsigned length;
    Color colors[]; 
} ColorArray;

typedef struct {
    unsigned length;
    uint8_t transp[]; 
} TranspArray;

static inline ColorArray *color_array_create(unsigned size) {
  ColorArray *color_array;
  color_array = malloc(sizeof(ColorArray) + sizeof(Color) * size);
  color_array->length = size;
  return color_array;
}

static inline void color_array_destroy(ColorArray *array) {
  if (array)
    free(array);
}

/**
 * @brief Creates a new ImageData structure with allocated memory.
 * @param width Image width.
 * @param height Image height.
 * @param bpp Bytes per pixel (1=Gray, 2=Gray+Alpha, 3=RGB, 4=RGBA).
 * @return Initialized ImageData (data is NULL on failure).
 */
ImageData image_create(unsigned int width, unsigned int height, unsigned int bpp);

/**
 * @brief Safely frees memory allocated for ImageData.
 * @param image Pointer to the image to destroy.
 */
void image_destroy(ImageData *image);

/**
 * @brief Creates a deep copy of an image.
 */
ImageData copy_image(ImageData orig);

/**
 * @brief Crops an image to a specified rectangular region.
 */
ImageData image_crop(const ImageData* src, unsigned int x, unsigned int y, unsigned int width, unsigned int height);

/**
 * @brief Blends a foreground image over a background using a mask.
 * Both images must be RGB and of the same size.
 */
ImageData blend_images(ImageData bg, ImageData fg, ImageData mask);

/**
 * @brief Applies global histogram equalization to the image.
 */
void image_apply_histogram(ImageData im);

/**
 * @brief Applies CLAHE (Contrast Limited Adaptive Histogram Equalization).
 * @param im Image to process (modified in-place).
 * @param tiles_x Number of horizontal tiles (typically 8).
 * @param tiles_y Number of vertical tiles (typically 8).
 * @param clip_limit Contrast limit factor (typically 2.0-4.0, higher = more contrast).
 */
void image_apply_clahe(ImageData im, int tiles_x, int tiles_y, float clip_limit);

/**
 * @brief Resamples image using bilinear interpolation (upsampling).
 * @param src Source image.
 * @param factor Upsampling factor (> 1).
 * @return New upsampled image.
 */
ImageData image_upsample_bilinear(const ImageData* src, int factor);

/**
 * @brief Resamples image using box filter (downsampling).
 * @param src Source image.
 * @param factor Downsampling factor (> 1).
 * @return New downsampled image.
 */
ImageData image_downsample_boxfilter(const ImageData* src, int factor);

/**
 * @brief Creates an alpha mask from DataF (255 = valid data, 0 = NonData).
 * @param data Pointer to DataF structure.
 * @return Single channel image (mask).
 */
ImageData image_create_alpha_mask_from_dataf(const void* data);

/**
 * @brief Adds an alpha channel to an image using a mask.
 * Converts bpp 1->2 or 3->4.
 */
ImageData image_add_alpha_channel(const ImageData* src, const ImageData* alpha_mask);

/**
 * @brief Expands an indexed image (bpp=1 or 2) to RGB/RGBA using a palette.
 * @param src Source indexed image.
 * @param palette Color array to map indices to colors.
 * @return Expanded RGB(A) image.
 */
ImageData image_expand_palette(const ImageData* src, const ColorArray* palette);

#endif /* HPSATVIEWS_IMAGE_H_ */
