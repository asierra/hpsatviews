#ifndef SINGLEGRAY_H
#define SINGLEGRAY_H

#include "image.h"
#include "datanc.h"
#include "reader_cpt.h"
#include <stdbool.h>

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha, const CPTData* cpt);

ImageData create_single_grayb(DataB c01, bool invert_value, bool use_alpha, const CPTData* cpt);

// Variante con rango personalizado (para expresiones algebraicas)
ImageData create_single_gray_range(DataF c01, bool invert_value, bool use_alpha, 
                                   float min_val, float max_val);

#endif
