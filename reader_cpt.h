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
    ColorEntry foreground;
    ColorEntry background;
    ColorEntry nan_color;
    int entry_count;
    ColorEntry entries[MAX_COLOR_ENTRIES];
    bool has_foreground;
    bool has_background;
    bool has_nan_color;
} CPTData;

CPTData* read_cpt_file(const char* filename);

ColorArray* cpt_to_color_array(CPTData* cpt);

void free_cpt_data(CPTData* cpt);

Color get_color_for_value(const CPTData* cpt, double value);
