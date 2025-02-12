#ifndef _READER_NC_H_
#define _READER_NC_H_

#include "datanc.h"

int load_nc_sf(char *filename, DataNC *datanc, char *variable, int planck);
int load_nc_float(char *filename, DataNCF *datanc, char *variable);
int compute_navigation_nc(char *filename, DataNCF *navla, DataNCF *navlo);

#endif
