/* Creates a dataset in geographics from a geostationary dataset.
 * LUT-BASED REPROJECTION - Final, Correct & Optimized Version
 *
 * This version uses a Look-Up Table (LUT) for reprojection, which is a
 * standard and efficient method for this task. It uses a well-estimated
 * search window to find the nearest source pixel for each destination pixel.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "datanc.h"
#include "reader_nc.h"
#include "writer_png.h"
#include "logger.h"
#include "image.h"
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

ImageData create_single_gray(DataF c01, bool invert_value, bool use_alpha);

// Holds the definition of the target geographic grid
typedef struct {
    float lat_min, lat_max;
    float lon_min, lon_max;
    unsigned lat_points, lon_points;
    float lat_resolution, lon_resolution;
} GeographicGrid;

// Creates the target geographic grid definition
GeographicGrid create_geographic_grid(DataF navla, DataF navlo, float resolution_deg) {
    GeographicGrid grid;
    grid.lat_min = navla.fmin;
    grid.lat_max = navla.fmax;
    grid.lon_min = navlo.fmin;
    grid.lon_max = navlo.fmax;
    grid.lat_resolution = resolution_deg;
    grid.lon_resolution = resolution_deg;
    grid.lat_points = (unsigned)round((grid.lat_max - grid.lat_min) / grid.lat_resolution) + 1;
    grid.lon_points = (unsigned)round((grid.lon_max - grid.lon_min) / grid.lon_resolution) + 1;

    const unsigned MAX_GRID_POINTS = 10848 * 2; // Allow larger grids if needed
    if (grid.lat_points > MAX_GRID_POINTS || grid.lon_points > MAX_GRID_POINTS) {
        LOG_WARN("Grid too large: %u x %u. Applying safety limits.", grid.lon_points, grid.lat_points);
        if (grid.lat_points > MAX_GRID_POINTS) grid.lat_points = MAX_GRID_POINTS;
        if (grid.lon_points > MAX_GRID_POINTS) grid.lon_points = MAX_GRID_POINTS;
        LOG_WARN("Adjusted to: %u x %u points.", grid.lon_points, grid.lat_points);
    }
    LOG_INFO("Geographic grid created: %u x %u points.", grid.lon_points, grid.lat_points);
    return grid;
}

// Forward mapping reprojection using precomputed navigation arrays (navla/navlo)
// For each source pixel, compute its destination grid cell and accumulate.
DataF forward_reproject(DataF datag, DataF navla, DataF navlo, const GeographicGrid* grid) {
    LOG_INFO("Starting forward-mapping reprojection...");
    double t0 = omp_get_wtime();

    const unsigned outW = grid->lon_points;
    const unsigned outH = grid->lat_points;
    const unsigned long long outSize = (unsigned long long)outW * outH;

    // Accumulators for averaging when multiple source pixels map to the same cell
    double *sum = (double*)calloc(outSize, sizeof(double));
    unsigned int *cnt = (unsigned int*)calloc(outSize, sizeof(unsigned int));
    if (!sum || !cnt) {
        LOG_ERROR("Failed to allocate accumulators for forward mapping");
        if (sum) free(sum);
        if (cnt) free(cnt);
        // Return empty image
        DataF empty = dataf_create(outW, outH);
        return empty;
    }

    const float lat_min = grid->lat_min;
    const float lon_min = grid->lon_min;
    const float lat_res = grid->lat_resolution;
    const float lon_res = grid->lon_resolution;

    // Iterate over source pixels and splat into destination grid
    #pragma omp parallel for
    for (long long idx = 0; idx < (long long)datag.size; idx++) {
        float v = datag.data_in[idx];
        float la = navla.data_in[idx];
        float lo = navlo.data_in[idx];

        if (v == NonData || la == NonData || lo == NonData) continue;
        if (la < grid->lat_min || la > grid->lat_max || lo < grid->lon_min || lo > grid->lon_max) continue;

        // Compute destination indices (nearest cell)
        int j = (int)floorf((la - lat_min) / lat_res + 0.5f);
        int i = (int)floorf((lo - lon_min) / lon_res + 0.5f);

        if (i < 0 || j < 0 || i >= (int)outW || j >= (int)outH) continue;
        unsigned long long oidx = (unsigned long long)j * outW + (unsigned long long)i;

        // Atomic accumulation to avoid data races
        #pragma omp atomic
        sum[oidx] += v;
        #pragma omp atomic
        cnt[oidx]++;
    }

    // Build output image from accumulators
    DataF datanc = dataf_create(outW, outH);
    datanc.fmin = datag.fmin;
    datanc.fmax = datag.fmax;

    unsigned long long valid = 0ULL;
    #pragma omp parallel for reduction(+:valid)
    for (long long oidx = 0; oidx < (long long)outSize; oidx++) {
        if (cnt[oidx] > 0) {
            datanc.data_in[oidx] = (float)(sum[oidx] / (double)cnt[oidx]);
            valid++;
        } else {
            datanc.data_in[oidx] = NonData;
        }
    }

    free(sum);
    free(cnt);

    double t1 = omp_get_wtime();
    LOG_INFO("Forward reprojection completed in %.2f seconds", t1 - t0);
    LOG_INFO("Valid pixels: %llu / %llu (%.1f%% coverage)", valid, outSize, 100.0 * (double)valid / (double)outSize);

    return datanc;
}

// Remap wrapper that uses forward mapping (kept name for minimal changes upstream)
DataF remap_using_lut(DataF datag, const GeographicGrid* grid, DataF navla, DataF navlo) {
    return forward_reproject(datag, navla, navlo, grid);
}


int main(int argc, char *argv[]) {
    logger_init(LOG_INFO);
    if (argc < 2) {
        LOG_ERROR("Usage: %s <NetCDF ABI File> [resolution_deg]", argv[0]);
        return -1;
    }

    const char *fnc = argv[1];
    LOG_INFO("Processing file: %s", fnc);

    DataNC dc;
    if (load_nc_sf(fnc, "Rad", &dc) != 0) return -1;
    LOG_INFO("Loaded NetCDF data: %u x %u pixels", dc.base.width, dc.base.height);

    DataF navlo, navla;
    if (compute_navigation_nc(fnc, &navla, &navlo) != 0) return -1;
    LOG_INFO("Navigation data computed successfully");

    // Calculate real navigation bounds by iterating through the data
    float real_lat_min = 999.0f, real_lat_max = -999.0f, real_lon_min = 999.0f, real_lon_max = -999.0f;
    for (unsigned i = 0; i < navla.size; i++) {
        if (navla.data_in[i] != NonData) {
            if (navla.data_in[i] < real_lat_min) real_lat_min = navla.data_in[i];
            if (navla.data_in[i] > real_lat_max) real_lat_max = navla.data_in[i];
        }
        if (navlo.data_in[i] != NonData) {
            if (navlo.data_in[i] < real_lon_min) real_lon_min = navlo.data_in[i];
            if (navlo.data_in[i] > real_lon_max) real_lon_max = navlo.data_in[i];
        }
    }
    navla.fmin = real_lat_min; navla.fmax = real_lat_max;
    navlo.fmin = real_lon_min; navlo.fmax = real_lon_max;
    LOG_INFO("Actual navigation extent: lat[%.3f, %.3f], lon[%.3f, %.3f]", navla.fmin, navla.fmax, navlo.fmin, navlo.fmax);

    // Determine output resolution
    float resolution_deg;
    if (argc > 2) {
        resolution_deg = atof(argv[2]);
        LOG_INFO("Using specified resolution: %.4f degrees", resolution_deg);
    } else {
        if ((navla.fmax - navla.fmin) > 150.0) { // Heuristic for full disk
            resolution_deg = 0.02f;
            LOG_INFO("Full disk detected, using default resolution: %.4f", resolution_deg);
        } else {
            resolution_deg = 0.01f; // Higher res for CONUS/Meso
            LOG_INFO("Regional scan detected, using default resolution: %.4f", resolution_deg);
        }
    }

    // Create grid and perform the reprojection
    GeographicGrid grid = create_geographic_grid(navla, navlo, resolution_deg);
    DataF datagg = remap_using_lut(dc.base, &grid, navla, navlo);
    
    // Create and save the image
    ImageData imout = create_single_gray(datagg, true, true);
    const char *outfn = "geographic_reprojection.png";
    if (write_image_png(outfn, &imout) == 0) {
        LOG_INFO("Output saved to: %s", outfn);
    }

    // Clean up
    dataf_destroy(&dc.base);
    dataf_destroy(&datagg);
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    image_destroy(&imout);

    return 0;
}
