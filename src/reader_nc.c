/* GOES-R ABI NetCDF reader: L1b radiance and L2 derived products.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "datanc.h"
#include "logger.h"
#include <math.h>
#include <netcdf.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERRCODE 2
#define ERR(e)                                                                                     \
    {                                                                                              \
        LOG_ERROR("NetCDF error: %s", nc_strerror(e));                                             \
        return (ERRCODE);                                                                          \
    }
#define WRN(e)                                                                                     \
    {                                                                                              \
        LOG_WARN("NetCDF warning: %s", nc_strerror(e));                                            \
    }

SatelliteID detect_satellite_from_filename(const char* filename) {
    if (!filename) return SAT_UNKNOWN;
    
    const char* sat_ptr = strstr(filename, "_G");
    if (sat_ptr) {
        if (strncmp(sat_ptr, "_G16", 4) == 0) return SAT_GOES16;
        if (strncmp(sat_ptr, "_G17", 4) == 0) return SAT_GOES17;
        if (strncmp(sat_ptr, "_G18", 4) == 0) return SAT_GOES18;
        if (strncmp(sat_ptr, "_G19", 4) == 0) return SAT_GOES19;
    }
    if (strstr(filename, "G16") || strstr(filename, "GOES16") || strstr(filename, "GOES-16")) {
        return SAT_GOES16;
    }
    if (strstr(filename, "G18") || strstr(filename, "GOES18") || strstr(filename, "GOES-18")) {
        return SAT_GOES18;
    }
    if (strstr(filename, "G17") || strstr(filename, "GOES17") || strstr(filename, "GOES-17")) {
        return SAT_GOES17;
    }
    if (strstr(filename, "G19") || strstr(filename, "GOES19") || strstr(filename, "GOES-19")) {
        return SAT_GOES19;
    }
    return SAT_UNKNOWN;
}

SectorID detect_sector_from_filename(const char *filename) {
    if (!filename) return SECTOR_UNKNOWN;
    // Mesoscale checked first: RadM1/RadM2 must not match RadC/RadF partially.
    if (strstr(filename, "RadM1") || strstr(filename, "CMIPM1")) return SECTOR_M1;
    if (strstr(filename, "RadM2") || strstr(filename, "CMIPM2")) return SECTOR_M2;
    if (strstr(filename, "RadC-") || strstr(filename, "CMIPC-")) return SECTOR_CONUS;
    if (strstr(filename, "RadF-") || strstr(filename, "CMIPF-")) return SECTOR_FD;
    return SECTOR_UNKNOWN;
}

typedef struct {
    float scale_factor;
    float add_offset;
    short fillvalue;
    nc_type var_type;
    /* Constants for L1b calibration */
    float planck_fk1, planck_fk2, planck_bc1, planck_bc2;
    float kappa0;
} NCScaleConfig;

/// Phase 1 - Heuristic engine: guesses the data variable for L2 products with no known mapping.
static const char* detect_generic_l2_variable(int ncid) {
    int y_dimid, x_dimid, nvars;
    if (nc_inq_dimid(ncid, "y", &y_dimid) != NC_NOERR ||
        nc_inq_dimid(ncid, "x", &x_dimid) != NC_NOERR) {
        return NULL;
    }

    nc_inq_nvars(ncid, &nvars);
    static char vname[NC_MAX_NAME + 1];

    for (int i = 0; i < nvars; i++) {
        int ndims, dimids[NC_MAX_VAR_DIMS];
        nc_inq_var(ncid, i, vname, NULL, &ndims, dimids, NULL);

        if (ndims != 2) continue;
        if (dimids[0] != y_dimid || dimids[1] != x_dimid) continue;

        if (strstr(vname, "DQF") || strstr(vname, "_bounds") || 
            strstr(vname, "time") || strcmp(vname, "x") == 0 || 
            strcmp(vname, "y") == 0) {
            continue;
        }
        return vname;
    }
    return NULL;
}

/// Maps a filename substring ("product") to the NetCDF variable holding its data; a time-saving shortcut for detect_generic_l2_variable's heuristic, not strictly required.
typedef struct {
    const char *product;
    const char *variable;
} ProductVariable;

static const ProductVariable PRODUCT_VARIABLES[] = {
    {"L1b",  "Rad"},
    {"CMIP", "CMI"},
    {"ACHA", "HT"},
    {"ACHT", "TEMP"},
    {"ACTP", "Phase"},
    {"CTP",  "PRES"},
    {"LST",  "LST"},
    {"SST",  "SST"},
    {NULL, NULL}
};

static const ProductVariable* detect_product_variable_from_filename(const char *filename) {
    if (!filename) return NULL;
    for (int i = 0; PRODUCT_VARIABLES[i].variable != NULL; i++) {
        if (strstr(filename, PRODUCT_VARIABLES[i].product)) return &PRODUCT_VARIABLES[i];
    }
    return NULL;
}

/// Phase 2 - Safe identification: resolves satellite, sector, and data variable id from the filename and NetCDF contents.
static int datanc_identify_product(int ncid, const char *filename, DataNC *datanc) {
    const ProductVariable *pv = detect_product_variable_from_filename(filename);
    const char *vname = pv ? pv->variable : NULL;
    int varid;

    datanc->sat_id = detect_satellite_from_filename(filename);
    datanc->sector_id = detect_sector_from_filename(filename);

    if (!vname) vname = detect_generic_l2_variable(ncid);

    if (!vname || nc_inq_varid(ncid, vname, &varid) != NC_NOERR) return -1;

    datanc->varname = strdup(vname);
    datanc->level = (strcmp(vname, "Rad") == 0) ? LEVEL_L1b : LEVEL_L2;
    // L1b ("Rad") has no distinct product identity beyond the channel/band.
    if (datanc->level == LEVEL_L2 && pv) datanc->product_name = pv->product;
    LOG_DEBUG("Detected variable id %d %s", varid, vname);
    return varid;
}

/// Phase 3 - Metadata reading: dimensions, calibration coefficients, and GEOS projection parameters.
static int datanc_read_metadata(int ncid, int varid, DataNC *datanc, NCScaleConfig *cfg) {
    int xid, yid, time_varid, band_varid, proj_varid;
    size_t w, h;

    if (nc_inq_dimid(ncid, "x", &xid) || nc_inq_dimid(ncid, "y", &yid)) return -1;
    nc_inq_dimlen(ncid, xid, &w);
    nc_inq_dimlen(ncid, yid, &h);
    datanc->fdata.width = (unsigned int)w;
    datanc->fdata.height = (unsigned int)h;

    if (nc_get_att_float(ncid, varid, "scale_factor", &cfg->scale_factor)) cfg->scale_factor = 1.0f;
    if (nc_get_att_float(ncid, varid, "add_offset", &cfg->add_offset)) cfg->add_offset = 0.0f;
    if (nc_get_att_short(ncid, varid, "_FillValue", &cfg->fillvalue)) cfg->fillvalue = -1;
    nc_inq_vartype(ncid, varid, &cfg->var_type);

    if (nc_inq_varid(ncid, "t", &time_varid) == NC_NOERR) {
        double tiempo = 0.0;
        if (nc_get_var_double(ncid, time_varid, &tiempo) == NC_NOERR)
            datanc->timestamp = (time_t)(946728000 + (long)tiempo);
    }

    if (nc_inq_varid(ncid, "band_id", &band_varid) == NC_NOERR) {
        int bid = 0; size_t idx = 0;
        if (nc_get_var1_int(ncid, band_varid, &idx, &bid) == NC_NOERR)
            datanc->band_id = (uint8_t)bid;
    }

    if (datanc->level == LEVEL_L1b) {
        int v1, v2, v3, v4;
        if (datanc->band_id >= 7) {
            if (nc_inq_varid(ncid, "planck_fk1", &v1) == NC_NOERR) nc_get_var_float(ncid, v1, &cfg->planck_fk1);
            if (nc_inq_varid(ncid, "planck_fk2", &v2) == NC_NOERR) nc_get_var_float(ncid, v2, &cfg->planck_fk2);
            if (nc_inq_varid(ncid, "planck_bc1", &v3) == NC_NOERR) nc_get_var_float(ncid, v3, &cfg->planck_bc1);
            if (nc_inq_varid(ncid, "planck_bc2", &v4) == NC_NOERR) nc_get_var_float(ncid, v4, &cfg->planck_bc2);
        } else {
            if (nc_inq_varid(ncid, "kappa0", &v1) == NC_NOERR) nc_get_var_float(ncid, v1, &cfg->kappa0);
        }
    }

    if (nc_inq_varid(ncid, "goes_imager_projection", &proj_varid) == NC_NOERR) {
        datanc->proj_code = PROJ_GEOS;
        nc_get_att_double(ncid, proj_varid, "perspective_point_height", &datanc->proj_info.sat_height);
        nc_get_att_double(ncid, proj_varid, "semi_major_axis", &datanc->proj_info.semi_major);
        nc_get_att_double(ncid, proj_varid, "semi_minor_axis", &datanc->proj_info.semi_minor);
        nc_get_att_double(ncid, proj_varid, "longitude_of_projection_origin", &datanc->proj_info.lon_origin);
        nc_get_att_double(ncid, proj_varid, "inverse_flattening", &datanc->proj_info.inv_flat);
        datanc->proj_info.valid = true;
        // x/y are coordinate VARIABLES (not just dimensions): need their own varid lookup.
        int xvar, yvar;
        double x_sf = 1.0, x_ao = 0.0, y_sf = 1.0, y_ao = 0.0;
        short  x0 = 0, y0 = 0;
        size_t iz = 0;
        bool gt_ok =
            nc_inq_varid(ncid, "x", &xvar)                         == NC_NOERR &&
            nc_inq_varid(ncid, "y", &yvar)                         == NC_NOERR &&
            nc_get_att_double(ncid, xvar, "scale_factor", &x_sf)   == NC_NOERR &&
            nc_get_att_double(ncid, xvar, "add_offset",   &x_ao)   == NC_NOERR &&
            nc_get_att_double(ncid, yvar, "scale_factor", &y_sf)   == NC_NOERR &&
            nc_get_att_double(ncid, yvar, "add_offset",   &y_ao)   == NC_NOERR &&
            nc_get_var1_short(ncid, xvar, &iz, &x0)                == NC_NOERR &&
            nc_get_var1_short(ncid, yvar, &iz, &y0)                == NC_NOERR;
        if (gt_ok) {
            datanc->geotransform[0] = ((double)x0 * x_sf + x_ao) - (x_sf / 2.0);
            datanc->geotransform[1] = x_sf;
            datanc->geotransform[2] = 0.0;
            datanc->geotransform[3] = ((double)y0 * y_sf + y_ao) - (y_sf / 2.0);
            datanc->geotransform[4] = 0.0;
            datanc->geotransform[5] = y_sf;
        } else {
            LOG_WARN("Geotransform not readable; marking projection as invalid.");
            datanc->proj_info.valid = false;
        }
    }

    char spatial_res_str[128] = {0};
    datanc->native_resolution_km = 0.0f;
    if (nc_get_att_text(ncid, NC_GLOBAL, "spatial_resolution", spatial_res_str) == NC_NOERR) {
        spatial_res_str[127] = '\0';
        float res_val;
        if (sscanf(spatial_res_str, "%fkm", &res_val) == 1) datanc->native_resolution_km = res_val;
    }
    return 0;
}

/// Phase 4 - Unpacking and parallelization: converts raw packed integers to calibrated floats.
static int datanc_unpack_grid(int ncid, int varid, size_t total_size, DataNC *datanc, const NCScaleConfig *cfg) {
    size_t tsize = (cfg->var_type == NC_BYTE || cfg->var_type == NC_UBYTE) ? 1 : 2;
    void *datatmp = malloc(tsize * total_size);
    if (!datatmp) return -1;

    if (nc_get_var(ncid, varid, datatmp) != NC_NOERR) { free(datatmp); return -1; }

    if (cfg->var_type == NC_BYTE || cfg->var_type == NC_UBYTE) {
        datanc->is_float = false;
        datanc->bdata = datab_create(datanc->fdata.width, datanc->fdata.height);
        int8_t *src = (int8_t *)datatmp;
        #pragma omp parallel for
        for (size_t i = 0; i < total_size; i++) {
            if (src[i] == (int8_t)cfg->fillvalue) datanc->bdata.data_in[i] = -128;
            else datanc->bdata.data_in[i] = src[i];
        }
    } else {
        datanc->is_float = true;
        datanc->fdata = dataf_create(datanc->fdata.width, datanc->fdata.height);
        float local_min = 1e30f, local_max = -1e30f;
        if (cfg->var_type == NC_USHORT) {
            unsigned short *src = (unsigned short *)datatmp;
            #pragma omp parallel for reduction(min:local_min) reduction(max:local_max)
            for (size_t i = 0; i < total_size; i++) {
                if (src[i] == (unsigned short)cfg->fillvalue) {
                    datanc->fdata.data_in[i] = NonData;
                } else {
                    float val = src[i] * cfg->scale_factor + cfg->add_offset;
                    if (datanc->level == LEVEL_L1b) {
                        if (datanc->band_id >= 7) val = (val > 0.0f) ? (cfg->planck_fk2 / (logf((cfg->planck_fk1 / val) + 1.0f)) - cfg->planck_bc1) / cfg->planck_bc2 : 0.0f;
                        else val *= cfg->kappa0;
                    }
                    datanc->fdata.data_in[i] = val;
                    if (val < local_min) local_min = val;
                    if (val > local_max) local_max = val;
                }
            }
        } else {
            short *src = (short *)datatmp;
            #pragma omp parallel for reduction(min:local_min) reduction(max:local_max)
            for (size_t i = 0; i < total_size; i++) {
                if (src[i] == cfg->fillvalue) {
                    datanc->fdata.data_in[i] = NonData;
                } else {
                    float val = src[i] * cfg->scale_factor + cfg->add_offset;
                    if (datanc->level == LEVEL_L1b) {
                        if (datanc->band_id >= 7) val = (val > 0.0f) ? (cfg->planck_fk2 / (logf((cfg->planck_fk1 / val) + 1.0f)) - cfg->planck_bc1) / cfg->planck_bc2 : 0.0f;
                        else val *= cfg->kappa0;
                    }
                    datanc->fdata.data_in[i] = val;
                    if (val < local_min) local_min = val;
                    if (val > local_max) local_max = val;
                }
            }
        }
        datanc->fdata.fmin = local_min; datanc->fdata.fmax = local_max;
    }
    free(datatmp);
    return 0;
}

/// Phase 5 - Final orchestration: open, identify, read metadata, unpack, and clean up.
int load_nc_sf(const char *filename, DataNC *datanc) {
    int ncid, varid, status = -1;
    NCScaleConfig cfg = { .scale_factor = 1.0f, .add_offset = 0.0f, .fillvalue = -1, .var_type = NC_SHORT };

    if (datanc != NULL) {
		memset(datanc, 0, sizeof(DataNC));
        datanc->proj_info.valid = false;
    }
    
    if (nc_open(filename, NC_NOWRITE, &ncid) != NC_NOERR) {
        LOG_ERROR("Error opening NetCDF: %s", filename);
        return -1;
    }

    varid = datanc_identify_product(ncid, filename, datanc);
    if (varid < 0) {
        LOG_WARN("Skipped or unsupported product: %s", filename);
        goto cleanup;
    }

    if (datanc_read_metadata(ncid, varid, datanc, &cfg) != 0) goto cleanup;

    size_t total_size = (size_t)datanc->fdata.width * (size_t)datanc->fdata.height;
    LOG_INFO("NetCDF dimensions: %ux%u (total: %zu)", datanc->fdata.width, datanc->fdata.height, total_size);
    if (datanc_unpack_grid(ncid, varid, total_size, datanc, &cfg) != 0) goto cleanup;

    status = 0;
cleanup:
    nc_close(ncid);
    if (status != 0) LOG_FATAL("NetCDF read pipeline failed for %s", filename);
    return status;
}


double rad2deg = 180.0 / M_PI;
double hsat, sm_maj, sm_min, lambda_0, H;

void compute_lalo(double x, double y, double *la, double *lo) {
    double snx, sny, csx, csy, sx, sy, sz, rs, a, b, c;
    double sm_maj2 = sm_maj * sm_maj;
    double sm_min2 = sm_min * sm_min;

    snx = sin(x);
    csx = cos(x);
    sny = sin(y);
    csy = cos(y);
    a = snx * snx + csx * csx * (csy * csy + sm_maj2 * sny * sny / sm_min2);
    b = -2.0 * H * csx * csy;
    c = H * H - sm_maj2;
    rs = (-b - sqrt(b * b - 4.0 * a * c)) / (2.0 * a);
    sx = rs * csx * csy;
    sy = -rs * snx;
    sz = rs * csx * sny;

    *la =
        (double)(atan2(sm_maj2 * sz, sm_min2 * sqrt(((H - sx) * (H - sx)) + (sy * sy))) * rad2deg);
    double lon_rad = lambda_0 - atan2(sy, H - sx);

    // Normalize longitude to [-PI, PI] before converting to degrees.
    lon_rad = fmod(lon_rad + M_PI, 2.0 * M_PI);
    if (lon_rad < 0)
        lon_rad += 2.0 * M_PI;
    *lo = (double)((lon_rad - M_PI) * rad2deg);
}

int compute_navigation_nc(const char *filename, DataF *navla, DataF *navlo) {
    int ncid, varid;
    int retval;

    if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
        ERR(retval);

    int xid, yid;
    if ((retval = nc_inq_dimid(ncid, "x", &xid)))
        ERR(retval);
    if ((retval = nc_inq_dimid(ncid, "y", &yid)))
        ERR(retval);

    size_t width, height;
    if ((retval = nc_inq_dimlen(ncid, xid, &width)))
        ERR(retval);
    if ((retval = nc_inq_dimlen(ncid, yid, &height)))
        ERR(retval);
    *navla = dataf_create(width, height);
    navla->size = navla->width * navla->height;
    navlo->width = navla->width;
    navlo->height = navla->height;
    navlo->size = navla->size;

    if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid)))
        ERR(retval);
    if ((retval = nc_get_att_double(ncid, varid, "perspective_point_height", &hsat)))
        ERR(retval);
    if ((retval = nc_get_att_double(ncid, varid, "semi_major_axis", &sm_maj)))
        ERR(retval);
    if ((retval = nc_get_att_double(ncid, varid, "semi_minor_axis", &sm_min)))
        ERR(retval);

    double lo_proj_orig = 0.0;
    if ((retval = nc_get_att_double(ncid, varid, "longitude_of_projection_origin", &lo_proj_orig)))
        ERR(retval);
    H = sm_maj + hsat;
    lambda_0 = lo_proj_orig / rad2deg;

    double x_sf = 1.0, y_sf = 1.0, x_ao = 0.0, y_ao = 0.0;

    // Re-resolve x/y as coordinate VARIABLES (not dimensions) to read their scale_factor/add_offset;
    // reuses xid/yid, which now hold varids instead of the dimids fetched above.
    if ((retval = nc_inq_varid(ncid, "x", &xid)))
        ERR(retval);
    if ((retval = nc_inq_varid(ncid, "y", &yid)))
        ERR(retval);

    if ((retval = nc_get_att_double(ncid, xid, "scale_factor", &x_sf))) ERR(retval);
    if ((retval = nc_get_att_double(ncid, xid, "add_offset",   &x_ao))) ERR(retval);
    if ((retval = nc_get_att_double(ncid, yid, "scale_factor", &y_sf))) ERR(retval);
    if ((retval = nc_get_att_double(ncid, yid, "add_offset",   &y_ao))) ERR(retval);

    short *x_vals_raw = malloc(width * sizeof(short));
    short *y_vals_raw = malloc(height * sizeof(short));

    if (!x_vals_raw || !y_vals_raw) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        LOG_ERROR("Memory error while reading x[], y[]");
        return -1;
    }

    if ((retval = nc_get_var_short(ncid, xid, x_vals_raw))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR(retval);
    }
    if ((retval = nc_get_var_short(ncid, yid, y_vals_raw))) {
        free(x_vals_raw);
        free(y_vals_raw);
        nc_close(ncid);
        ERR(retval);
    }

    *navlo = dataf_create(width, height);

    // Precompute sin/cos per column (x) and row (y) to avoid recomputing them
    // 'height' and 'width' times, respectively.
    double *snx_arr = malloc(width  * sizeof(double));
    double *csx_arr = malloc(width  * sizeof(double));
    double *sny_arr = malloc(height * sizeof(double));
    double *csy_arr = malloc(height * sizeof(double));
    if (!snx_arr || !csx_arr || !sny_arr || !csy_arr) {
        free(snx_arr); free(csx_arr); free(sny_arr); free(csy_arr);
        free(x_vals_raw); free(y_vals_raw);
        nc_close(ncid);
        LOG_ERROR("Memory error while precomputing navigation sin/cos");
        return -1;
    }
    for (size_t i = 0; i < width; i++) {
        double x = (double)x_vals_raw[i] * x_sf + x_ao;
        snx_arr[i] = sin(x);
        csx_arr[i] = cos(x);
    }
    for (size_t j = 0; j < height; j++) {
        double y = (double)y_vals_raw[j] * y_sf + y_ao;
        sny_arr[j] = sin(y);
        csy_arr[j] = cos(y);
    }

    const double sm_maj2 = sm_maj * sm_maj;
    const double sm_min2 = sm_min * sm_min;
    const double ratio   = sm_maj2 / sm_min2;
    const double H2_maj2 = H * H - sm_maj2;

    double lomin = 1e10, lamin = 1e10, lomax = -lomin, lamax = -lamin;
    size_t valid_count = 0;

    double t0 = omp_get_wtime();
#pragma omp parallel for schedule(static) \
    reduction(min : lamin, lomin) reduction(max : lamax, lomax) reduction(+ : valid_count)
    for (size_t j = 0; j < navla->height; j++) {
        double sny = sny_arr[j], csy = csy_arr[j];
        double csy2 = csy * csy, rat_sny2 = ratio * sny * sny;
        for (size_t i = 0; i < navla->width; i++) {
            double snx = snx_arr[i], csx = csx_arr[i];
            double a   = snx * snx + csx * csx * (csy2 + rat_sny2);
            double b   = -2.0 * H * csx * csy;
            double disc = b * b - 4.0 * a * H2_maj2;
            size_t k = j * navla->width + i;
            if (disc < 0.0) {
                navla->data_in[k] = NonData;
                navlo->data_in[k] = NonData;
            } else {
                double rs  = (-b - sqrt(disc)) / (2.0 * a);
                double px  = rs * csx * csy;
                double py  = -rs * snx;
                double pz  = rs * csx * sny;
                double la  = atan2(sm_maj2 * pz,
                                   sm_min2 * sqrt((H - px) * (H - px) + py * py)) * rad2deg;
                double lo = (lambda_0 - atan2(py, H - px)) * rad2deg;
                navla->data_in[k] = (float)la;
                navlo->data_in[k] = (float)lo;
                if (la < lamin) lamin = la;
                if (la > lamax) lamax = la;
                if (lo < lomin) lomin = lo;
                if (lo > lomax) lomax = lo;
                valid_count++;
            }
        }
    }
    free(snx_arr); free(csx_arr); free(sny_arr); free(csy_arr);
    LOG_TIMING(omp_get_wtime() - t0, "Navigation (%zux%zu)", navla->width, navla->height);

    // Update lat/lon range only if valid pixels were found.
    if (valid_count > 0) {
        navla->fmin = (float)lamin;
        navla->fmax = (float)lamax;
        navlo->fmin = (float)lomin;
        navlo->fmax = (float)lomax;
    } else {
        // No valid pixels: fall back to global extent defaults.
        navla->fmin = -90.0f;
        navla->fmax = 90.0f;
        navlo->fmin = -180.0f;
        navlo->fmax = 180.0f;
        LOG_WARN("No valid navigation pixels in compute_navigation_nc; using default extents.");
    }

    free(x_vals_raw);
    free(y_vals_raw);

    if ((retval = nc_close(ncid)))
        ERR(retval);

    return 0;
}

int create_navigation_from_reprojected_bounds(DataF *navla, DataF *navlo, size_t width,
                                              size_t height, float lon_min, float lon_max,
                                              float lat_min, float lat_max) {
    *navla = dataf_create(width, height);
    *navlo = dataf_create(width, height);
    if (navla->data_in == NULL || navlo->data_in == NULL) {
        LOG_FATAL("Memory allocation failed for navigation grids of reprojected data.");
        return -1;
    }

    float lat_range = lat_max - lat_min;
    float lon_range = lon_max - lon_min;

#pragma omp parallel for collapse(2)
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t i = y * width + x;
            navlo->data_in[i] = lon_min + ((float)x / (float)(width - 1)) * lon_range;
            navla->data_in[i] = lat_max - ((float)y / (float)(height - 1)) * lat_range;
        }
    }
    navla->fmin = lat_min;
    navla->fmax = lat_max;
    navlo->fmin = lon_min;
    navlo->fmax = lon_max;
    return 0;
}

/// Computes solar zenith/azimuth angle for a given lat/lon and UTC time; uses the same algorithm as sun_zenith_angle in daynight_mask.c for consistency.
static void compute_sun_geometry(float la, float lo, int year, int month, int day, int hour,
                                 int min, int sec, double *zenith_out, double *azimuth_out) {
    double t, te, wte, s1, c1, s2, c2, s3, c3, sp, cp, sd, cd, cH, se0, ep, De, lambda, epsi, sl,
        cl, se, ce, L, nu, Dlam;
    int yt, mt;
    double RightAscension, Declination, HourAngle, Zenith, Azimuth;

    double PI = M_PI;
    double PI2 = 2 * M_PI;
    double PIM = M_PI_2;

    double UT = hour + min / 60.0 + sec / 3600.0;
    double Longitude = lo * PI / 180.0;
    double Latitude = la * PI / 180.0;
    double Pressure = 1;
    double Temperature = 0;

    if (month <= 2) {
        mt = month + 12;
        yt = year - 1;
    } else {
        mt = month;
        yt = year;
    }

    t = (double)((int)(365.25 * (double)(yt - 2000)) + (int)(30.6001 * (double)(mt + 1)) -
                 (int)(0.01 * (double)(yt)) + day) +
        0.0416667 * UT - 21958.0;
    double Dt = 96.4 + 0.00158 * t;
    te = t + 1.1574e-5 * Dt;

    wte = 0.0172019715 * te;

    s1 = sin(wte);
    c1 = cos(wte);
    s2 = 2.0 * s1 * c1;
    c2 = (c1 + s1) * (c1 - s1);
    s3 = s2 * c1 + c2 * s1;
    c3 = c2 * c1 - s2 * s1;

    L = 1.7527901 + 1.7202792159e-2 * te + 3.33024e-2 * s1 - 2.0582e-3 * c1 + 3.512e-4 * s2 -
        4.07e-5 * c2 + 5.2e-6 * s3 - 9e-7 * c3 - 8.23e-5 * s1 * sin(2.92e-5 * te) +
        1.27e-5 * sin(1.49e-3 * te - 2.337) + 1.21e-5 * sin(4.31e-3 * te + 3.065) +
        2.33e-5 * sin(1.076e-2 * te - 1.533) + 3.49e-5 * sin(1.575e-2 * te - 2.358) +
        2.67e-5 * sin(2.152e-2 * te + 0.074) + 1.28e-5 * sin(3.152e-2 * te + 1.547) +
        3.14e-5 * sin(2.1277e-1 * te - 0.488);

    nu = 9.282e-4 * te - 0.8;
    Dlam = 8.34e-5 * sin(nu);
    lambda = L + PI + Dlam;

    epsi = 4.089567e-1 - 6.19e-9 * te + 4.46e-5 * cos(nu);

    sl = sin(lambda);
    cl = cos(lambda);
    se = sin(epsi);
    ce = sqrt(1 - se * se);

    RightAscension = atan2(sl * ce, cl);
    if (RightAscension < 0.0)
        RightAscension += PI2;

    Declination = asin(sl * se);

    HourAngle = 1.7528311 + 6.300388099 * t + Longitude - RightAscension + 0.92 * Dlam;
    HourAngle = fmod(HourAngle + PI, PI2) - PI;
    if (HourAngle < -PI)
        HourAngle += PI2;

    sp = sin(Latitude);
    cp = sqrt((1 - sp * sp));
    sd = sin(Declination);
    cd = sqrt(1 - sd * sd);
    double sH = sin(HourAngle);
    cH = cos(HourAngle);
    se0 = sp * sd + cp * cd * cH;
    ep = asin(se0) - 4.26e-5 * sqrt(1.0 - se0 * se0);
    Azimuth = atan2(sH, cH * sp - sd * cp / cd);

    if (ep > 0.0)
        De = (0.08422 * Pressure) / ((273.0 + Temperature) * tan(ep + 0.003138 / (ep + 0.08919)));
    else
        De = 0.0;

    Zenith = PIM - ep - De;

    if (zenith_out)
        *zenith_out = Zenith * 180.0 / M_PI;
    if (azimuth_out)
        *azimuth_out = Azimuth * 180.0 / M_PI;
}

/// Computes satellite viewing zenith/azimuth angle for a pixel, from the geometry between the pixel and the geostationary sub-satellite position.
static void compute_satellite_view_angles(float pixel_lat, float pixel_lon, float sat_lon,
                                          float sat_height, double *vza_out, double *vaa_out) {
    const double a = 6378137.0;           // WGS84 semi-major axis (m)
    const double f = 1.0 / 298.257223563; // WGS84 flattening

    double lat_rad = pixel_lat * M_PI / 180.0;
    double lon_rad = pixel_lon * M_PI / 180.0;
    double sat_lon_rad = sat_lon * M_PI / 180.0;

    // Pixel position in geocentric Cartesian coordinates.
    double N = a / sqrt(1.0 - (2.0 * f - f * f) * sin(lat_rad) * sin(lat_rad));
    double x_pixel = N * cos(lat_rad) * cos(lon_rad);
    double y_pixel = N * cos(lat_rad) * sin(lon_rad);
    double z_pixel = N * (1.0 - (2.0 * f - f * f)) * sin(lat_rad);

    // Geostationary satellite position (equatorial plane, at sat_lon).
    double sat_radius = a + sat_height;
    double x_sat = sat_radius * cos(sat_lon_rad);
    double y_sat = sat_radius * sin(sat_lon_rad);
    double z_sat = 0.0;

    // Unit view vector from satellite to pixel.
    double dx = x_pixel - x_sat;
    double dy = y_pixel - y_sat;
    double dz = z_pixel - z_sat;
    double dist = sqrt(dx * dx + dy * dy + dz * dz);
    dx /= dist; dy /= dist; dz /= dist;

    // Local surface normal at the pixel.
    double n_len = sqrt(x_pixel * x_pixel + y_pixel * y_pixel + z_pixel * z_pixel);
    double nx = x_pixel / n_len;
    double ny = y_pixel / n_len;
    double nz = z_pixel / n_len;

    // VZA: angle between view direction and surface normal.
    // Negative sign because view_direction points toward pixel (downward).
    double cos_vza = -(dx * nx + dy * ny + dz * nz);
    double vza = acos(fmax(-1.0, fmin(1.0, cos_vza))) * 180.0 / M_PI;

    // VAA: projection onto local ENU (East-North-Up) frame.
    double east_x = -sin(lon_rad);
    double east_y = cos(lon_rad);
    double east_z = 0.0;

    double north_x = -sin(lat_rad) * cos(lon_rad);
    double north_y = -sin(lat_rad) * sin(lon_rad);
    double north_z = cos(lat_rad);

    double view_east = dx * east_x + dy * east_y + dz * east_z;
    double view_north = dx * north_x + dy * north_y + dz * north_z;
    double vaa = atan2(view_east, view_north) * 180.0 / M_PI;

    if (vza_out)
        *vza_out = vza;
    if (vaa_out)
        *vaa_out = vaa;
}

int compute_solar_angles_nc(const char *filename, const DataF *navla, const DataF *navlo,
                            DataF *sza, DataF *saa) {
    int ncid, retval;

    if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
        ERR(retval);

    int time_varid;
    if ((retval = nc_inq_varid(ncid, "t", &time_varid)))
        ERR(retval);
    double tiempo;
    if ((retval = nc_get_var_double(ncid, time_varid, &tiempo)))
        ERR(retval);

    LOG_DEBUG("J2000 time read from NetCDF: %.1f seconds", tiempo);

    // Convertir tiempo J2000 a fecha/hora
    long tt = (long)(tiempo);
    time_t dt = 946728000 + tt; // Epoch J2000
    struct tm ts = *gmtime(&dt);
    int year = ts.tm_year + 1900;
    int month = ts.tm_mon + 1;
    int day = ts.tm_mday;
    int hour = ts.tm_hour;
    int min = ts.tm_min;
    int sec = ts.tm_sec;

    if ((retval = nc_close(ncid)))
        ERR(retval);

    LOG_DEBUG("Date/time for solar calculation: %04d-%02d-%02d %02d:%02d:%02d UTC", year, month, day,
              hour, min, sec);

    *sza = dataf_create(navla->width, navla->height);
    *saa = dataf_create(navla->width, navla->height);

    if (sza->data_in == NULL || saa->data_in == NULL) {
        LOG_FATAL("Memory allocation failed for solar angle maps.");
        return ERRCODE;
    }

    LOG_INFO("Computing solar geometry for %04d-%02d-%02d %02d:%02d:%02d UTC", year, month, day,
             hour, min, sec);

    double start_time = omp_get_wtime();

// Per-pixel solar geometry calculation.
#pragma omp parallel for
    for (size_t i = 0; i < navla->size; i++) {
        float la = navla->data_in[i];
        float lo = navlo->data_in[i];
        if (IS_NONDATA(la) || IS_NONDATA(lo)) {
            sza->data_in[i] = NonData;
            saa->data_in[i] = NonData;
        } else {
            double zen, azi;
            compute_sun_geometry(la, lo, year, month, day, hour, min, sec, &zen, &azi);
            sza->data_in[i] = (float)zen;
            saa->data_in[i] = (float)azi;
        }
    }

    double elapsed = omp_get_wtime() - start_time;
    LOG_TIMING(elapsed, "Solar geometry");

    return 0;
}

int compute_satellite_angles_nc(const char *filename, const DataF *navla, const DataF *navlo,
                                DataF *vza, DataF *vaa) {
    int ncid, varid, retval;

    if ((retval = nc_open(filename, NC_NOWRITE, &ncid)))
        ERR(retval);

    // Read satellite projection parameters (longitude of sub-satellite point, orbital altitude).
    if ((retval = nc_inq_varid(ncid, "goes_imager_projection", &varid)))
        ERR(retval);

    float sat_lon, sat_height_km;
    if ((retval = nc_get_att_float(ncid, varid, "longitude_of_projection_origin", &sat_lon)))
        ERR(retval);
    if ((retval = nc_get_att_float(ncid, varid, "perspective_point_height", &sat_height_km)))
        ERR(retval);

    if ((retval = nc_close(ncid)))
        ERR(retval);

    LOG_INFO("Computing satellite geometry (sub-point: %.1f°E, altitude: %.0f km)", sat_lon,
             sat_height_km);

    *vza = dataf_create(navla->width, navla->height);
    *vaa = dataf_create(navla->width, navla->height);

    if (vza->data_in == NULL || vaa->data_in == NULL) {
        LOG_FATAL("Memory allocation failed for satellite angle maps.");
        return ERRCODE;
    }

    double start_time = omp_get_wtime();

// Per-pixel satellite viewing geometry calculation.
#pragma omp parallel for
    for (size_t i = 0; i < navla->size; i++) {
        float la = navla->data_in[i];
        float lo = navlo->data_in[i];
        if (IS_NONDATA(la) || IS_NONDATA(lo)) {
            vza->data_in[i] = NonData;
            vaa->data_in[i] = NonData;
        } else {
            double vzen, vazi;
            compute_satellite_view_angles(la, lo, sat_lon, sat_height_km, &vzen, &vazi);
            vza->data_in[i] = (float)vzen;
            vaa->data_in[i] = (float)vazi;
        }
    }

    double elapsed = omp_get_wtime() - start_time;
    LOG_TIMING(elapsed, "Satellite geometry");

    return 0;
}

void compute_relative_azimuth(const DataF *saa, const DataF *vaa, DataF *raa) {
    *raa = dataf_create(saa->width, saa->height);

    if (raa->data_in == NULL) {
        LOG_FATAL("Memory allocation failed for relative azimuth map.");
        return;
    }

#pragma omp parallel for
    for (size_t i = 0; i < saa->size; i++) {
        float sa = saa->data_in[i];
        float va = vaa->data_in[i];

        if (sa == NonData || va == NonData) {
            raa->data_in[i] = NonData;
        } else {
            float diff = fabsf(sa - va);

            if (diff > 180.0f) {
                diff = 360.0f - diff;
            }

            raa->data_in[i] = diff;
        }
    }

    LOG_INFO("Relative azimuth computed for %zu pixels.", raa->size);
}
