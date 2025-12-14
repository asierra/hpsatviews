/* NetCDF Data reader
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_READER_NC_H_
#define HPSATVIEWS_READER_NC_H_

#include "datanc.h"


// Load GOES L1b data and metadada from nc file
int load_nc_sf(const char *filename, const char *variable, DataNC *datanc);

// Just load float array from nc file
int load_nc_float(const char *filename, DataF *datanc, const char *variable);

// From L1b file compute local navigation
int compute_navigation_nc(const char *GOES_L1b_filename, DataF *navla, DataF *navlo);

// Create navigation grids for an already reprojected (geographic) grid
int create_navigation_from_reprojected_bounds(DataF *navla, DataF *navlo, size_t width, size_t height, float lon_min, float lon_max, float lat_min, float lat_max);

DataF dataf_load_from_netcdf(const char *filename, const char *varname);

// Compute solar geometry angles (Solar Zenith Angle and Solar Azimuth Angle)
int compute_solar_angles_nc(const char *filename, const DataF *navla, const DataF *navlo, DataF *sza, DataF *saa);

// Compute satellite viewing geometry angles (View Zenith Angle and View Azimuth Angle)
int compute_satellite_angles_nc(const char *filename, const DataF *navla, const DataF *navlo, DataF *vza, DataF *vaa);

// Compute relative azimuth angle between sun and satellite
void compute_relative_azimuth(const DataF *saa, const DataF *vaa, DataF *raa);

#endif /* HPSATVIEWS_READER_NC_H_ */
