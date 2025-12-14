#ifndef DAYNIGHT_MASK_H
#define DAYNIGHT_MASK_H

#include "image.h"
#include "datanc.h"

ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo, float *dnratio, float max_temp);

#endif
