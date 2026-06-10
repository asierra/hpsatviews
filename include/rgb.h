/* RGB and day/night composite generation for ABI multi-band imagery.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_RGB_H_
#define HPSATVIEWS_RGB_H_

#include <stdbool.h>
#include "channelset.h"
#include "datanc.h"
#include "image.h"
#include "config.h"
#include "metadata.h"

// Forward declarations
typedef struct ArgParser ArgParser;

// Parsed CLI options for the rgb command.
typedef struct {
    const char *input_file;        // anchor NetCDF file path
    const char *mode;              // composite mode: "truecolor", "ash", "airmass", "night", etc.
    char *output_filename;         // NULL = auto-generated
    bool output_generated;         // true if output_filename was malloc'd

    bool do_reprojection;          // --geographics
    bool save_both;                // --both: save fixed-grid and reprojected
    bool has_clip;                 // --clip present
    float clip_coords[4];          // [lon_min, lat_max, lon_max, lat_min]

    float gamma[3];                // per-channel gamma (default 1.0); "v1;v2;v3" syntax for R;G;B
    bool apply_histogram;          // --histo
    bool apply_clahe;              // --clahe
    int clahe_tiles_x;             // default 8
    int clahe_tiles_y;             // default 8
    float clahe_clip_limit;        // default 4.0
    int scale;                     // --scale (negative=down, positive=up, 1=unchanged)

    bool apply_rayleigh;           // --rayleigh (truecolor/composite only)
    bool rayleigh_analytic;        // --ray-analytic (analytical formula instead of LUT)
    bool use_piecewise_stretch;    // --stretch
    bool use_sharpen;              // --sharpen (ratio sharpening)
    bool use_citylights;           // --citylights (night/composite only)
    bool use_alpha;                // --alpha
    bool force_geotiff;            // --geotiff
    bool use_full_res;             // --full-res
    float cloud_temp;              // --cloud-temp/-T: cloud IR threshold (K); 0=disabled

    char *expr;                    // --expr band algebra expression
    char *minmax;                  // --minmax per-channel range override

    bool is_l2_product;            // auto-detected from filename (contains "CMIP")
} RgbOptions;

// Full state container for an RGB composite operation.
typedef struct {
    RgbOptions opts;

    ChannelSet *channel_set;       // set of required ABI channel filenames
    char id_signature[40];         // scene timestamp token extracted from input_file

    DataNC channels[17];           // loaded channel data (indices 1-16; [0] unused)
    int ref_channel_idx;           // highest-resolution channel loaded

    DataF nav_lat;
    DataF nav_lon;
    bool has_navigation;

    float final_lon_min, final_lon_max;
    float final_lat_min, final_lat_max;
    unsigned crop_x_offset, crop_y_offset; // pixel offset for native-grid GeoTIFF with clip

    DataF comp_r;
    DataF comp_g;
    DataF comp_b;

    float min_r, max_r;
    float min_g, max_g;
    float min_b, max_b;

    ImageData final_image;
    ImageData alpha_mask;

    bool error_occurred;
    char error_msg[512];
} RgbContext;

// RGB composer function pointer type.
typedef bool (*RgbComposer)(RgbContext *ctx);

// Descriptor for a composite RGB strategy (mode name, required channels, composer).
typedef struct {
    const char *mode_name;           // e.g., "ash", "truecolor"
    const char *req_channels[8];     // NULL-terminated list, e.g. {"C11","C13","C14","C15",NULL}
    RgbComposer composer_func;
    const char *description;         // shown in help output
    bool needs_navigation;           // true if navla/navlo grids are required
} RgbStrategy;

// Initializes an RgbContext to default values.
void rgb_context_init(RgbContext *ctx);

// Frees all dynamic memory in an RgbContext. Safe with NULL pointers.
void rgb_context_destroy(RgbContext *ctx);

// Parses CLI arguments into RgbOptions within ctx. Returns true on success.
bool rgb_parse_options(ArgParser *parser, RgbContext *ctx);

// Runs the full RGB composite pipeline.
// cfg: immutable process configuration. meta: mutable metadata accumulator.
// Returns 0 on success.
int run_rgb(const ProcessConfig *cfg, MetadataContext *meta);

// Combines three float grids into an 8-bit RGB image with per-channel linear stretch.
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

#endif /* HPSATVIEWS_RGB_H_ */
