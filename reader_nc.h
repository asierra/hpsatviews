/* NetCDF Data reader
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef _READER_NC_H_
#define _READER_NC_H_

#include "datanc.h"

int load_nc_sf(char *filename, char *variable, DataNC *datanc);

int load_nc_float(char *filename, DataF *datanc, char *variable);

int compute_navigation_nc(char *filename, DataF *navla, DataF *navlo);

#endif
