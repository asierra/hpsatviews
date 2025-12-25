#include "parse_expr.h"
#include "logger.h"


static void skip_spaces(const char **ptr) {
    while (isspace(**ptr)) (*ptr)++;
}

int parse_expr_string(const char *input, LinearCombo *out) {
    if (!input || !out) return -1;

    out->num_terms = 0;
    out->bias = 0.0;
    
    const char *ptr = input;
    bool expect_operator = false;


    while (*ptr != '\0') {
        skip_spaces(&ptr);
        if (*ptr == '\0') break;

        double current_sign = 1.0;

        // 1. Manejo de operador
        if (*ptr == '+' || *ptr == '-') {
            if (*ptr == '-') current_sign = -1.0;
            ptr++;
            skip_spaces(&ptr);
            expect_operator = false;
        } else if (expect_operator) {
            LOG_ERROR("Se esperaba un operador (+ o -) en -> '%s'", ptr); // Caso "C13 C15"
            return -1;
        }

        // 2. Intentar leer un número (coeficiente o bias)
        char *next_ptr;
        if (isdigit(*ptr) || *ptr == '.') {
            double val = strtod(ptr, &next_ptr);
            if (ptr == next_ptr) {
                LOG_ERROR("Número mal formado en -> '%s'", ptr); // Caso "2.0.3*C13"
                return -1;
            }

            ptr = next_ptr;
            skip_spaces(&ptr);

            if (*ptr == '*') {
                ptr++;
                skip_spaces(&ptr);
                if (*ptr != 'C') {
                    LOG_ERROR("Se esperaba 'C' después de '*' en -> '%s'", ptr);
                    return -1;
                }
                ptr++;

                int bid = (int)strtol(ptr, &next_ptr, 10);
                if (ptr == next_ptr || bid < 1 || bid > 16) {
                    LOG_ERROR("Banda C%02d inválida (rango permitido: C01-C16) en -> '%s'", bid, ptr); // Caso "C99"
                    return -1;
                }

                if (out->num_terms >= 10) return -1;
                out->terms[out->num_terms].coeff = val * current_sign;
                out->terms[out->num_terms].band_id = (uint8_t)bid;
                out->num_terms++;
                ptr = next_ptr;
            } else {
                out->bias += val * current_sign;
            }
        }
        // 3. Banda con coeficiente implícito
        else if (*ptr == 'C') {
            ptr++;
            int bid = (int)strtol(ptr, &next_ptr, 10);
            if (ptr == next_ptr || bid < 1 || bid > 16) {
                LOG_ERROR("Banda C%02d inválida (rango permitido: C01-C16) en -> '%s'", bid, ptr);
                return -1;
            }

            if (out->num_terms >= 10) return -1;
            out->terms[out->num_terms].coeff = 1.0 * current_sign;
            out->terms[out->num_terms].band_id = (uint8_t)bid;
            out->num_terms++;
            ptr = next_ptr;
        } else {
            // Caso de caracteres prohibidos como / , ( , ^
            LOG_ERROR("Carácter o símbolo no soportado '%c' en -> '%s'", *ptr, ptr); // Caso "C13/C15"
            return -1;
        }

        expect_operator = true;
        skip_spaces(&ptr);
    }

    if (out->num_terms == 0) {
        LOG_ERROR("La expresión debe contener al menos una banda (C01-C16)");
        return -1;
    }

    return 0;
}

int extract_required_channels(const LinearCombo* combo, char** channels_out) {
    if (!combo || !channels_out) return 0;
    
    int unique_count = 0;
    bool seen[17] = {false}; // seen[1..16]
    
    for (int i = 0; i < combo->num_terms; i++) {
        uint8_t band_id = combo->terms[i].band_id;
        if (band_id < 1 || band_id > 16) continue; // Sanity check
        
        if (!seen[band_id]) {
            seen[band_id] = true;
            channels_out[unique_count] = malloc(4); // "CXX\0"
            if (channels_out[unique_count]) {
                snprintf(channels_out[unique_count], 4, "C%02d", band_id);
                unique_count++;
            }
        }
    }
    channels_out[unique_count] = NULL; // Terminador
    return unique_count;
}

/**
 * Evalúa una combinación lineal de bandas usando los canales cargados.
 * Implementa: result = coeff1*band1 + coeff2*band2 + ... + bias
 */
DataF evaluate_linear_combo(const LinearCombo* combo, const DataNC* channels) {
    if (!combo || !channels || combo->num_terms == 0) {
        DataF empty = {0};
        return empty;
    }
    
    // 1. Obtener dimensiones del primer canal válido
    int ref_idx = combo->terms[0].band_id;
    size_t width = channels[ref_idx].fdata.width;
    size_t height = channels[ref_idx].fdata.height;
    
    // 2. Crear resultado inicializado con bias
    DataF result = dataf_create(width, height);
    dataf_fill(&result, (float)combo->bias);

    // 3. Acumular cada término: result += coeff * channel
    for (int i = 0; i < combo->num_terms; i++) {
        uint8_t band_id = combo->terms[i].band_id;
        double coeff = combo->terms[i].coeff;

        // Multiplicar canal por coeficiente: scaled = channel * coeff
        DataF scaled = dataf_op_scalar(&channels[band_id].fdata, (float)coeff, OP_MUL, false);
        
        // Sumar al resultado: result = result + scaled
        DataF temp = dataf_op_dataf(&result, &scaled, OP_ADD);
        
        // Liberar temporales
        dataf_destroy(&result);
        dataf_destroy(&scaled);
        
        result = temp;
    }
    
    return result;
}


/**
 * Analiza una expresión compleja (ej: "2*C13; C14-C15; C01") y genera 
 * una lista de strings con los canales únicos requeridos (ej: "C01", "C13", "C14", "C15").
 * * @param full_expr La cadena completa de --expr
 * @param channels_out Puntero a un arreglo de strings (char***) que será asignado.
 * @return Número de canales encontrados.
 */
int get_unique_channels_rgb(const char *full_expr, char ***channels_out) {
    if (!full_expr) return 0;

    bool seen[17] = {false}; // Mapa de bits para bandas C01-C16
    int count = 0;

    // 1. Trabajar sobre una copia para no alterar el original con strtok
    char *expr_copy = strdup(full_expr);
    char *token = strtok(expr_copy, ";");

    // 2. Iterar sobre las 3 partes (R, G, B)
    while (token != NULL) {
        LinearCombo combo;
        // Reutilizamos tu parser existente para extraer las bandas de este segmento
        if (parse_expr_string(token, &combo) == 0) {
            for (int i = 0; i < combo.num_terms; i++) {
                int bid = combo.terms[i].band_id;
                if (bid >= 1 && bid <= 16 && !seen[bid]) {
                    seen[bid] = true;
                    count++;
                }
            }
        }
        token = strtok(NULL, ";");
    }
    free(expr_copy);

    // 3. Si no hay canales, salir
    if (count == 0) {
        *channels_out = NULL;
        return 0;
    }

    // 4. Crear el arreglo de strings (NULL terminated para compatibilidad con channelset)
    // Asignamos (count + 1) punteros
    *channels_out = (char**)malloc(sizeof(char*) * (count + 1));
    
    int idx = 0;
    for (int b = 1; b <= 16; b++) {
        if (seen[b]) {
            (*channels_out)[idx] = (char*)malloc(4); // "Cxx\0"
            snprintf((*channels_out)[idx], 4, "C%02d", b);
            idx++;
        }
    }
    (*channels_out)[idx] = NULL; // Terminador seguro

    return count;
}


#ifdef PARSE_EXPR_STANDALONE
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: %s \"expresion\"\n", argv[0]);
        return 1;
    }

    LinearCombo combo;
    if (parse_expr_string(argv[1], &combo) == 0) {
        printf("Parsing exitoso:\n");
        for (int i = 0; i < combo.num_terms; i++) {
            printf("  Término %d: Coeff=%.2f, Band=C%02d\n", i, combo.terms[i].coeff, combo.terms[i].band_id);
        }
        printf("  Bias: %.2f\n", combo.bias);

        // Probar extracción de bandas únicas
        char* required_channels[10];
        int n = extract_required_channels(&combo, required_channels);
        printf("Bandas requeridas (%d): ", n);
        for (int i = 0; i < n; i++) {
            printf("%s%s", required_channels[i], (i < n-1) ? ", " : "\n");
            free(required_channels[i]); // Liberar memoria
        }
    } else {
        printf("Error de sintaxis en la expresión.\n");
        return 1;
    }
    return 0;
}
#endif
