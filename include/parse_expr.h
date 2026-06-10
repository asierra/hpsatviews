/* Band algebra expression parser for custom multi-channel composites.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_PARSE_EXPR_H_
#define HPSATVIEWS_PARSE_EXPR_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "datanc.h"

typedef struct {
    uint8_t band_id;  // ABI band index 1-16, matching band_id in DataNC
    double coeff;     // linear coefficient (e.g., 2.0, -4.0)
} LinearTerm;

typedef struct {
    LinearTerm terms[10];   // up to 10 linear terms
    int        num_terms;   // number of active terms
    double     bias;        // constant bias term
} LinearCombo;

// Parses a band algebra string into a LinearCombo.
// Returns 0 on success, -1 on syntax error.
int parse_expr_string(const char *input, LinearCombo *out);

// Extracts the unique ABI band names required by a LinearCombo.
// channels_out must hold at least 10 elements (e.g., "C13", "C15").
// Returns the number of unique bands found.
int extract_required_channels(const LinearCombo* combo, char** channels_out);

// Evaluates a LinearCombo against a loaded channel array (indexed 1-16).
// Formula: result = coeff1*band1 + coeff2*band2 + ... + bias
// Returns a new DataF; caller must free with dataf_destroy().
DataF evaluate_linear_combo(const LinearCombo* combo, const DataNC* channels);

// Parses a multi-component expression (e.g., "2*C13; C14-C15; C01") and
// returns the list of unique bands required across all components.
// channels_out is heap-allocated; caller must free it.
// Returns the number of unique channels found.
int get_unique_channels_rgb(const char *full_expr, char ***channels_out);

#endif /* HPSATVIEWS_PARSE_EXPR_H_ */
