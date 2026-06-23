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

/// Forward declarations
typedef struct ArgParser ArgParser;

/// Parsed CLI options for the rgb command.
typedef struct {
    const char *input_file;        ///< Anchor NetCDF file path
    const char *mode;              ///< Composite mode (e.g., "truecolor", "ash")
    char *output_filename;         ///< Auto-generated if NULL
    bool output_generated;         ///< True if output_filename was dynamically allocated

    bool do_reprojection;
    bool save_both;
    bool has_clip;
    float clip_coords[4];          ///< [lon_min, lat_max, lon_max, lat_min]

    float gamma[3];                ///< Per-channel gamma (R;G;B)
    bool apply_histogram;
    bool apply_clahe;
    int clahe_tiles_x;
    int clahe_tiles_y;
    float clahe_clip_limit;
    int scale;

    bool apply_rayleigh;
    bool rayleigh_analytic;        ///< Use analytical formula instead of LUT
    bool use_piecewise_stretch;
    bool use_sharpen;
    bool use_citylights;
    bool use_alpha;
    bool force_geotiff;
    bool use_full_res;
    float cloud_temp;              ///< Cloud IR threshold (K); 0=disabled

    char *expr;                    ///< Band algebra expression
    char *minmax;                  ///< Per-channel range override

    bool is_l2_product;
} RgbOptions;

/// Full state container for an RGB composite operation.
typedef struct {
    RgbOptions opts;

    ChannelSet *channel_set;       ///< Required ABI channel filenames
    char id_signature[40];         ///< Scene timestamp token

    DataNC channels[17];           ///< Loaded channel data (indices 1-16; [0] unused)
    int ref_channel_idx;           ///< Highest-resolution channel loaded

    DataF nav_lat;
    DataF nav_lon;
    bool has_navigation;

    float final_lon_min, final_lon_max;
    float final_lat_min, final_lat_max;
    unsigned crop_x_offset, crop_y_offset; ///< Pixel offset for native-grid GeoTIFF

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

/// RGB composer function pointer.
typedef bool (*RgbComposer)(RgbContext *ctx);

/// Descriptor for a composite RGB strategy.
typedef struct {
    const char *mode_name;
    const char *req_channels[8];     ///< NULL-terminated list (e.g. {"C11","C13",NULL})
    RgbComposer composer_func;
    const char *description;
    bool needs_navigation;
} RgbStrategy;

/// Initializes an RgbContext to default values.
void rgb_context_init(RgbContext *ctx);

/// Frees all dynamic memory in an RgbContext.
void rgb_context_destroy(RgbContext *ctx);

/// Parses CLI arguments into RgbOptions within ctx.
bool rgb_parse_options(ArgParser *parser, RgbContext *ctx);

/**
 * Runs the full RGB composite pipeline.
 * 
 * @param cfg Immutable process configuration.
 * @param meta Mutable metadata accumulator.
 */
int run_rgb(const ProcessConfig *cfg, MetadataContext *meta);

/// Combines three float grids into an 8-bit RGB image with per-channel linear stretch.
ImageData create_multiband_rgb(const DataF* r_ch, const DataF* g_ch, const DataF* b_ch,
                               float r_min, float r_max, float g_min, float g_max,
                               float b_min, float b_max);

#endif /* HPSATVIEWS_RGB_H_ */
