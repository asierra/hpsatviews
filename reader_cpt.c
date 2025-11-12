/* To define the colormap for a pseudocolor image, we use GPT from GMT.
 * 
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
// https://docs.generic-mapping-tools.org/dev/reference/features.html#color-palette-tables

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "logger.h"
#include "reader_cpt.h"

// Ensure the color entries array has an even size for range processing.
static_assert(MAX_COLOR_ENTRIES % 2 == 0, "MAX_COLOR_ENTRIES must be an even number.");


// If the CPT is normalized, we use all the colors 
// Otherwise if CPT has limits and we use them, we adjust for
// 0 = B, 255 or all from values > max = F, NonData = N
ColorArray* cpt_to_color_array(CPTData* cpt) {
    if (!cpt || cpt->entry_count < 2) {
        return NULL;
    }

    // Define el tamaño de la paleta (256 colores es estándar para PNG con paleta)
    unsigned int palette_size = 256;

    // Asigna memoria para la estructura ColorArray y el arreglo flexible de colores
    ColorArray* color_array = color_array_create(palette_size);
    if (!color_array) {
        return NULL;
    }

    // Obtiene el rango de valores del CPT
    float min_val = cpt->entries[0].value;
    float max_val = cpt->entries[cpt->entry_count - 1].value;
    float range = max_val - min_val;

    // Genera la paleta de 256 colores interpolando desde el CPT
    for (unsigned int i = 0; i < palette_size; i++) {
        // Calcula el valor actual en el rango del CPT
        float value = min_val + (range * i) / (palette_size - 1);
        // Obtiene el color interpolado para ese valor
        color_array->colors[i] = get_color_for_value(cpt, value);
    }

    return color_array;
}


// Función para parsear una línea de entrada del CPT
bool parse_cpt_line(const char* line, ColorEntry* entry1, ColorEntry* entry2) {
    int result = sscanf(line, "%f %hhu %hhu %hhu %f %hhu %hhu %hhu",
                       &entry1->value, &entry1->color.r, &entry1->color.g, &entry1->color.b,
                       &entry2->value, &entry2->color.r, &entry2->color.g, &entry2->color.b);
    
    // A valid color line has 8 values.
    if (result == 8) {
        return true;
    }

    // Check for 4-value format (value r g b)
    result = sscanf(line, "%f %hhu %hhu %hhu", &entry1->value, &entry1->color.r, &entry1->color.g, &entry1->color.b);
    if (result == 4) {
        return true; // Indicates a 4-value line was parsed
    }
    
    return false;
}

// Función para parsear colores especiales (F, B, N)
bool parse_special_color(const char* line, CPTData* cpt) {
    char type;
    Color color;
    
    // Use a space in " %c" to consume leading whitespace
    if (sscanf(line, " %c %hhu %hhu %hhu", &type, &color.r, &color.g, &color.b) == 4) {
        switch (type) {
            case 'F':
                cpt->foreground = color;
                cpt->has_foreground = true;
                break;
            case 'B':
                cpt->background = color;
                cpt->has_background = true;
                break;
            case 'N':
                cpt->nan_color = color;
                cpt->has_nan_color = true;
                break;
            default:
                return false;
        }
        return true;
    }
    return false;
}

// Función principal para leer un archivo CPT
CPTData* read_cpt_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: No se pudo abrir el archivo %s\n", filename);
        return NULL;
    }
    
    CPTData* cpt = malloc(sizeof(CPTData));
    if (!cpt) {
        fclose(file);
        return NULL;
    }
    
    // Inicializar estructura
    memset(cpt, 0, sizeof(CPTData));
    cpt->entry_count = 0;
    cpt->has_foreground = false;
    cpt->has_background = false;
    cpt->has_nan_color = false;
    strcpy(cpt->name, filename);
    
    char line[MAX_LINE_LENGTH];
    bool in_header = true;
    
    while (fgets(line, sizeof(line), file)) {
        // Eliminar newline
        line[strcspn(line, "\n")] = 0;
        
        // Saltar líneas vacías o comentarios
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        
        // Procesar colores especiales (F, B, N)
        if (parse_special_color(line, cpt)) {
            continue;
        }
        
        // Procesar entradas de color
        ColorEntry entry1, entry2;
        int result = sscanf(line, "%f %hhu %hhu %hhu %f %hhu %hhu %hhu",
                       &entry1.value, &entry1.color.r, &entry1.color.g, &entry1.color.b,
                       &entry2.value, &entry2.color.r, &entry2.color.g, &entry2.color.b);

        if (result == 8) { // Full format line
            if (cpt->entry_count < MAX_COLOR_ENTRIES - 2) {
                cpt->entries[cpt->entry_count++] = entry1;
                cpt->entries[cpt->entry_count++] = entry2;
            }
        } else if (result >= 4) { // Short format line
            if (cpt->entry_count > 0 && cpt->entry_count < MAX_COLOR_ENTRIES - 1) {
                cpt->entries[cpt->entry_count - 1].value = entry1.value;
            }
            if (cpt->entry_count < MAX_COLOR_ENTRIES - 1) {
                cpt->entries[cpt->entry_count++] = entry1;
            }
        }
        else if (in_header) {
            // Asumir que es el nombre o metadata
            strncpy(cpt->name, line, sizeof(cpt->name) - 1);
        }
    }
    
    fclose(file);

    // If the last entry is a single point, duplicate it to form a valid final range.
    if (cpt->entry_count > 0 && cpt->entry_count % 2 != 0) {
        cpt->entries[cpt->entry_count] = cpt->entries[cpt->entry_count - 1];
        cpt->entry_count++;
    }
    return cpt;
}

// Función para liberar la memoria
void free_cpt_data(CPTData* cpt) {
    free(cpt);
}

// Función para imprimir información del CPT
void print_cpt_info(const CPTData* cpt) {
    printf("CPT: %s\n", cpt->name);
    printf("Entradas de color: %d\n", cpt->entry_count);
    
    if (cpt->has_foreground) {
        printf("Foreground: %u/%u/%u\n", 
               cpt->foreground.r, 
               cpt->foreground.g, 
               cpt->foreground.b);
    }
    
    if (cpt->has_background) {
        printf("Background: %u/%u/%u\n", 
               cpt->background.r, 
               cpt->background.g, 
               cpt->background.b);
    }
    
    if (cpt->has_nan_color) {
        printf("NaN Color: %u/%u/%u\n", 
               cpt->nan_color.r, 
               cpt->nan_color.g, 
               cpt->nan_color.b);
    }
    
    printf("\nTabla de colores:\n");
    for (int i = 0; i < cpt->entry_count; i += 2) {
        printf("%.6g -> %.6g: ", 
               cpt->entries[i].value, 
               cpt->entries[i+1].value);
        printf("RGB(%u,%u,%u) -> RGB(%u,%u,%u)\n",
               cpt->entries[i].color.r, cpt->entries[i].color.g, cpt->entries[i].color.b,
               cpt->entries[i+1].color.r, cpt->entries[i+1].color.g, cpt->entries[i+1].color.b);
    }
}

// Función para obtener el color interpolado para un valor dado
Color get_color_for_value(const CPTData* cpt, double value) {
    Color result = {0, 0, 0}; // Color por defecto (negro)
    
    // Verificar si el valor está fuera del rango
    if (cpt->entry_count < 2) {
        return result;
    }
    
    // Valor menor que el mínimo
    if (value < cpt->entries[0].value) {
        return cpt->has_background ? cpt->background : result;
    }
    
    // Valor mayor que el máximo
    if (value > cpt->entries[cpt->entry_count - 1].value) {
        return cpt->has_foreground ? cpt->foreground : result;
    }
    
    // Buscar el intervalo correcto
    for (int i = 0; i < cpt->entry_count - 1; i += 2) {
        if (value >= cpt->entries[i].value && value < cpt->entries[i + 1].value) {
            // Interpolar linealmente entre los dos colores
            double t = (value - cpt->entries[i].value) / 
                      (cpt->entries[i + 1].value - cpt->entries[i].value);
            
            result.r = (uint8_t)(cpt->entries[i].color.r + t * (cpt->entries[i + 1].color.r - cpt->entries[i].color.r));
            result.g = (uint8_t)(cpt->entries[i].color.g + t * (cpt->entries[i + 1].color.g - cpt->entries[i].color.g));
            result.b = (uint8_t)(cpt->entries[i].color.b + t * (cpt->entries[i + 1].color.b - cpt->entries[i].color.b));
            
            return result;
        }
    }
    // Handle value being exactly the max value
    if (cpt->entry_count > 0 && value == cpt->entries[cpt->entry_count - 1].value) {
        return cpt->entries[cpt->entry_count - 1].color;
    }
    
    return result;
}


#ifdef CPT_READER_MAIN
int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Uso: %s <archivo.cpt>\n", argv[0]);
        return 1;
    }
    
    CPTData* cpt = read_cpt_file(argv[1]);
    if (!cpt) {
        // El error específico ya fue impreso por read_cpt_file
        return 1;
    }
    
    // Mostrar información del CPT
    print_cpt_info(cpt);
        
    free_cpt_data(cpt);
    return 0;
}
#endif
