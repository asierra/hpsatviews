#ifndef READER_CPT_H_
#define READER_CPT_H_

#include <string.h>
#include "image.h"

#define MAX_LINE_LENGTH 256
#define MAX_COLOR_ENTRIES 512
#define MAX_HEADER_LINES 50


typedef struct {
    float value;
    Color color;
} ColorEntry;

typedef struct {
    char name[MAX_LINE_LENGTH];
    Color foreground;
    Color background;
    Color nan_color;
    // Must be 2, 4, 16, or 256
    unsigned int num_colors;
    unsigned int entry_count;
    ColorEntry entries[MAX_COLOR_ENTRIES];
    bool has_foreground;
    bool has_background;
    bool has_nan_color;
    bool is_discrete;
} CPTData;


static inline CPTData *cpt_create(unsigned int num_colors, bool has_nan_color) {
	CPTData* cpt = malloc(sizeof(CPTData));
	memset(cpt, 0, sizeof(CPTData));
    cpt->num_colors = num_colors;
    cpt->has_foreground = false;
    cpt->has_background = false;
    cpt->has_nan_color = has_nan_color;
    cpt->is_discrete = false;
  return cpt;
}

CPTData* read_cpt_file(const char* filename);

ColorArray* cpt_to_color_array(CPTData* cpt);

void free_cpt_data(CPTData* cpt);

Color get_color_for_value(const CPTData* cpt, double value);

#endif /* READER_CPT_H_ */
