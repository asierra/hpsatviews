/* Configuration header for HPSATVIEWS.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_CONFIG_H_
#define HPSATVIEWS_CONFIG_H_

#include <stdbool.h>

// Immutable processing configuration populated from parsed CLI arguments.
typedef struct {
    // Input
    const char *input_file;     // Path to the NetCDF anchor file
    bool is_l2_product;         // true if CMIP L2 product (inferred from filename)
    
    // Operation mode
    const char *command;        // "rgb", "gray", "pseudocolor"
    const char *strategy;       // composite recipe: "truecolor", "ash", "ch13", etc.
    
    // Radiometric enhancements
    float gamma[3];
    bool apply_clahe;
    float clahe_clip_limit;
    int clahe_tiles_x;
    int clahe_tiles_y;
    bool apply_histogram;
    bool apply_rayleigh;        // Rayleigh atmospheric correction (LUT-based)
    bool rayleigh_analytic;     // Analytic Rayleigh correction
    bool use_piecewise_stretch; // --stretch: piecewise contrast stretch
    bool use_sharpen;           // --sharpen: ratio sharpening
    bool invert_values;         // Invert scale (IR channels)
    
    // Compositing options
    int scale;                  // Integer scale factor (up- or down-sampling)
    bool use_alpha;             // Generate alpha channel
    bool use_citylights;        // Composite city-lights background (night)
    bool use_full_res;          // Full resolution output (L2 products)
    float cloud_temp;           // --cloud-temp: BT threshold (K); colder pixels treated as night (0 = disabled)

    // Band algebra (custom mode)
    bool is_custom_mode;
    const char *custom_expr;    // User expression, e.g. "0.5*C02+0.3*C03"
    const char *custom_minmax;  // Optional min,max range
    const char *product_short;  // {PROD} token: part before ':' in --name or short mode label
    const char *product_long;   // 'product' field in JSON/GeoTIFF metadata: part after ':' in --name
    
    // Pseudocolor
    const char *palette_file;   // Path to .cpt palette file (NULL if unused)
    
    // Spatial subset and reprojection
    bool has_clip;
    float clip_coords[4];       // [lon_min, lat_max, lon_max, lat_min]
    bool do_reprojection;
    bool save_both;             // -B: save fixed-grid and reprojected outputs (implies do_reprojection)
    
    // Output
    bool force_geotiff;
    const char *output_path_override; // NULL for automatic naming

} ProcessConfig;

// Forward declaration to avoid including args.h here.
typedef struct ArgParser ArgParser;

// Populates a zeroed ProcessConfig from a parsed ArgParser. Returns false on error.
bool config_from_argparser(ArgParser *parser, ProcessConfig *cfg);

// Validates logical consistency of the configuration. Returns false on error.
bool config_validate(const ProcessConfig *cfg);

void config_print_debug(const ProcessConfig *cfg);

// Frees dynamically allocated fields (output_path_override). Safe to call on a zeroed struct.
void config_destroy(ProcessConfig *cfg);

// Returns a malloc'd copy of path with "_geo" inserted before the extension. Caller must free.
char* insert_geo_suffix(const char *path);

#endif // HPSATVIEWS_CONFIG_H_
