#ifndef HPSATVIEWS_WRITER_PNG_H_
#define HPSATVIEWS_WRITER_PNG_H_

#include "image.h"

int write_image_png(const char *filename, ImageData *image);

int write_image_png_palette(const char *filename, ImageData *image, ColorArray *palette);

#endif /* HPSATVIEWS_WRITER_PNG_H_ */
