/* GOES-R ABI NetCDF reader: L1b radiance and L2 derived products.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_READER_NC_H_
#define HPSATVIEWS_READER_NC_H_

#include "datanc.h"


// Loads GOES ABI L1b or L2 data and metadata from a NetCDF file.
int load_nc_sf(const char *filename, DataNC *datanc);

// Loads a single float variable from a NetCDF file.
int load_nc_float(const char *filename, DataF *datanc, const char *variable);

// Computes lat/lon navigation grids from the GOES-R fixed-grid projection metadata.
int compute_navigation_nc(const char *GOES_L1b_filename, DataF *navla, DataF *navlo);

// Builds navigation grids for an already-reprojected geographic (equirectangular) grid.
int create_navigation_from_reprojected_bounds(DataF *navla, DataF *navlo, size_t width, size_t height, float lon_min, float lon_max, float lat_min, float lat_max);

DataF dataf_load_from_netcdf(const char *filename, const char *varname);

// Computes per-pixel Solar Zenith Angle (SZA) and Solar Azimuth Angle (SAA).
int compute_solar_angles_nc(const char *filename, const DataF *navla, const DataF *navlo, DataF *sza, DataF *saa);

// Computes per-pixel View Zenith Angle (VZA) and View Azimuth Angle (VAA).
int compute_satellite_angles_nc(const char *filename, const DataF *navla, const DataF *navlo, DataF *vza, DataF *vaa);

// Computes the Relative Azimuth Angle (RAA) between sun and satellite.
void compute_relative_azimuth(const DataF *saa, const DataF *vaa, DataF *raa);

#endif /* HPSATVIEWS_READER_NC_H_ */
