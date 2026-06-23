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
    uint8_t band_id;  ///< ABI band index 1-16
    double coeff;     ///< Linear coefficient (e.g., 2.0, -4.0)
} LinearTerm;

typedef struct {
    LinearTerm terms[10];   ///< Up to 10 linear terms
    int        num_terms;   ///< Number of active terms
    double     bias;        ///< Constant bias term
} LinearCombo;

/// Parses a band algebra string into a LinearCombo.
int parse_expr_string(const char *input, LinearCombo *out);

/// Extracts the unique ABI band names required by a LinearCombo.
int extract_required_channels(const LinearCombo* combo, char** channels_out);

/// Evaluates a LinearCombo against a loaded channel array.
DataF evaluate_linear_combo(const LinearCombo* combo, const DataNC* channels);

/// Parses a multi-component expression and returns the list of unique bands required.
int get_unique_channels_rgb(const char *full_expr, char ***channels_out);

#endif /* HPSATVIEWS_PARSE_EXPR_H_ */
