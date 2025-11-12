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

DataF dataf_load_from_netcdf(const char *filename, const char *varname);

#endif /* HPSATVIEWS_READER_NC_H_ */
