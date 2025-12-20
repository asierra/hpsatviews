#ifndef PARSE_EXPR_H
#define PARSE_EXPR_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "datanc.h"  // Para DataF y DataNC

typedef struct {
    uint8_t band_id;  // 1-16 (como band_id en DataNC)
    double coeff;     // Coeficiente: 2.0, -4.0, etc.
} LinearTerm;

typedef struct {
    LinearTerm terms[10];   // Máximo 10 términos
    int        num_terms;   // Número de términos usados (0-10)
    double     bias;        // Término independiente
} LinearCombo;

/**
 * Traduce una cadena a una estructura LinearCombo.
 * Retorna 0 si tiene éxito, -1 en error de sintaxis.
 */
int parse_expr_string(const char *input, LinearCombo *out);

/**
 * Extrae la lista de bandas únicas requeridas por una expresión.
 * 
 * @param combo Puntero a la estructura LinearCombo parseada.
 * @param channels_out Array de strings para almacenar nombres de canales (ej: "C13", "C15").
 *                     Debe tener espacio para al menos 10 elementos.
 * @return Número de bandas únicas encontradas.
 * 
 * Ejemplo:
 *   LinearCombo combo;
 *   parse_expr_string("2.0*C13 - 4.0*C15 + C13", &combo);
 *   char* channels[10];
 *   int n = extract_required_channels(&combo, channels);
 *   // n = 2, channels = ["C13", "C15", NULL, ...]
 */
int extract_required_channels(const LinearCombo* combo, char** channels_out);

/**
 * Evalúa una combinación lineal de bandas usando los canales cargados.
 * 
 * @param combo Puntero a la estructura LinearCombo con la expresión parseada.
 * @param channels Array de DataNC indexado por band_id [1..16].
 * @return DataF con el resultado de la evaluación. Debe liberarse con dataf_destroy().
 * 
 * Fórmula: result = coeff1*band1 + coeff2*band2 + ... + bias
 * 
 * Ejemplo:
 *   // Expresión: "2.0*C13 - 4.0*C15 + 0.5"
 *   DataF result = evaluate_linear_combo(&combo, channels);
 *   // result[i] = 2.0*channels[13].fdata[i] - 4.0*channels[15].fdata[i] + 0.5
 */
DataF evaluate_linear_combo(const LinearCombo* combo, const DataNC* channels);

#endif