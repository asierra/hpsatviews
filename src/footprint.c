/* Geographic footprint (EPSG:4326) of a raster on the satellite's fixed grid.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "footprint.h"
#include "logger.h"
#include <math.h>
#include <omp.h>
#include <string.h>

/// Iterations of the bisection that pulls an off-limb sample back onto the
/// Earth. 40 halvings take a full-disk radius (~5.4e6 m) below a micrometre,
/// so the limit is the projection's own conditioning, not this loop: right at
/// the limb 0.1 m of position is worth 0.03° of longitude.
#define LIMB_BISECTIONS 40

static double lon_wrap(double lon) {
    while (lon < -180.0) lon += 360.0;
    while (lon > 180.0) lon -= 360.0;
    return lon;
}

/// Longitude difference normalized to [-180, 180).
static double lon_delta(double lon, double ref) {
    double d = lon - ref;
    while (d < -180.0) d += 360.0;
    while (d >= 180.0) d -= 360.0;
    return d;
}

/* Inverse of the GOES-R fixed grid: scan angle to geodetic, the same closed
 * form compute_navigation_nc() runs per pixel (src/reader_nc.c:537-570) and
 * nav_cuda.cu mirrors on the GPU. Deliberately not GDAL's OCTTransform: it
 * gave identical coordinates to the last digit, but spent ~17 ms building the
 * PROJ pipeline — a tenth of a small run, for 260 points. */
typedef struct {
    double H;          /* semi-major axis + satellite height (m) */
    double lambda_0;   /* projection origin longitude (rad) */
    double sm_maj2, sm_min2, ratio, H2_maj2;
    double per_metre;  /* metres on the projection plane -> scan angle (rad) */
} GeosInverse;

static void geos_inverse_init(GeosInverse *g, const DataNC *ref) {
    const double sm_maj = ref->proj_info.semi_major;
    const double sm_min = ref->proj_info.semi_minor;
    g->H = sm_maj + ref->proj_info.sat_height;
    g->lambda_0 = ref->proj_info.lon_origin * M_PI / 180.0;
    g->sm_maj2 = sm_maj * sm_maj;
    g->sm_min2 = sm_min * sm_min;
    g->ratio = g->sm_maj2 / g->sm_min2;
    g->H2_maj2 = g->H * g->H - g->sm_maj2;
    g->per_metre = 1.0 / ref->proj_info.sat_height;
}

/// One point from metres on the fixed grid to lon/lat. Returns false when the
/// ray misses the Earth: the discriminant going negative IS the limb test.
static bool project_point(const GeosInverse *g, double x, double y,
                          double *lon, double *lat) {
    const double xr = x * g->per_metre, yr = y * g->per_metre;
    const double snx = sin(xr), csx = cos(xr);
    const double sny = sin(yr), csy = cos(yr);

    const double a = snx * snx + csx * csx * (csy * csy + g->ratio * sny * sny);
    const double b = -2.0 * g->H * csx * csy;
    const double disc = b * b - 4.0 * a * g->H2_maj2;
    if (disc < 0.0 || a == 0.0) return false;

    const double rs = (-b - sqrt(disc)) / (2.0 * a);
    const double px = rs * csx * csy;
    const double py = -rs * snx;
    const double pz = rs * csx * sny;

    const double la = atan2(g->sm_maj2 * pz,
                            g->sm_min2 * sqrt((g->H - px) * (g->H - px) + py * py));
    const double lo = g->lambda_0 - atan2(py, g->H - px);
    if (!isfinite(la) || !isfinite(lo)) return false;

    *lat = la * 180.0 / M_PI;
    *lon = lon_wrap(lo * 180.0 / M_PI);
    return true;
}

/// Last point on the Earth along the segment from the (valid) centre towards an
/// off-limb border sample: this is how the ring follows the limb.
static bool bisect_to_limb(const GeosInverse *g, double cx, double cy,
                           double px, double py, double *lon, double *lat) {
    double lo = 0.0, hi = 1.0;   /* lo: centre, on the Earth; hi: border, off it */
    for (int i = 0; i < LIMB_BISECTIONS; i++) {
        double mid = 0.5 * (lo + hi);
        double dlon, dlat;
        if (project_point(g, cx + (px - cx) * mid, cy + (py - cy) * mid, &dlon, &dlat)) lo = mid;
        else hi = mid;
    }
    return project_point(g, cx + (px - cx) * lo, cy + (py - cy) * lo, lon, lat);
}

/// Exact point where the segment from a valid sample to an invalid one leaves
/// the Earth, bisected along the segment itself. This is the raster corner the
/// limb cuts off, and it usually carries the extreme latitude or longitude.
static bool bisect_along_edge(const GeosInverse *g, double vx, double vy,
                              double ix, double iy, double *lon, double *lat) {
    double lo = 0.0, hi = 1.0;   /* lo: the valid end; hi: the invalid one */
    for (int i = 0; i < LIMB_BISECTIONS; i++) {
        double mid = 0.5 * (lo + hi);
        double dlon, dlat;
        if (project_point(g, vx + (ix - vx) * mid, vy + (iy - vy) * mid, &dlon, &dlat)) lo = mid;
        else hi = mid;
    }
    return project_point(g, vx + (ix - vx) * lo, vy + (iy - vy) * lo, lon, lat);
}

/// bbox from the ring. Longitudes are accumulated as offsets from a reference
/// meridian (the subsatellite point) instead of as raw values: a GOES-West disk
/// spans -218°..-56°, whose raw min/max is [-179, +176] — the whole planet.
/// Measured from lon_0 the span is the true ±81°, and the wrap is then folded
/// back into the STAC convention of west > east.
static void ring_bbox(Footprint *fp, double lon_ref) {
    double dmin = 180.0, dmax = -180.0;
    double lat_min = 90.0, lat_max = -90.0;
    for (int i = 0; i < fp->count; i++) {
        double d = lon_delta(fp->lon[i], lon_ref);
        if (d < dmin) dmin = d;
        if (d > dmax) dmax = d;
        if (fp->lat[i] < lat_min) lat_min = fp->lat[i];
        if (fp->lat[i] > lat_max) lat_max = fp->lat[i];
    }
    fp->bbox[0] = lon_wrap(lon_ref + dmin);
    fp->bbox[1] = lat_min;
    fp->bbox[2] = lon_wrap(lon_ref + dmax);
    fp->bbox[3] = lat_max;
    fp->crosses_antimeridian = (fp->bbox[0] > fp->bbox[2]);
}

static void push(Footprint *fp, double lon, double lat) {
    if (fp->count < FOOTPRINT_MAX_POINTS) {
        fp->lon[fp->count] = lon;
        fp->lat[fp->count] = lat;
        fp->count++;
    }
}

int footprint_from_geos_box(const DataNC *ref, double x_min, double y_min,
                            double x_max, double y_max, Footprint *out) {
    if (ref == NULL || out == NULL) return 1;
    memset(out, 0, sizeof(*out));
    if (ref->proj_code != PROJ_GEOS || !ref->proj_info.valid ||
        ref->proj_info.sat_height == 0.0 || ref->proj_info.semi_minor == 0.0) {
        LOG_INFO("No geographic footprint: the file carries no usable projection.");
        return 1;
    }

    const double t0 = omp_get_wtime();
    GeosInverse inv;
    geos_inverse_init(&inv, ref);
    const GeosInverse *g = &inv;

    const double cx = 0.5 * (x_min + x_max);
    const double cy = 0.5 * (y_min + y_max);
    double clon, clat;
    if (!project_point(g, cx, cy, &clon, &clat)) {
        // Every limb bisection anchors on the centre, so an off-limb centre
        // means the raster does not see the Earth at all.
        LOG_WARN("The raster centre falls outside the visible disk; no footprint.");
        return 1;
    }

    // The border as one closed loop of samples, so the seam between the last
    // edge and the first is handled like any other pair.
    const int n = 4 * FOOTPRINT_EDGE_SAMPLES;
    double bx[4 * FOOTPRINT_EDGE_SAMPLES], by[4 * FOOTPRINT_EDGE_SAMPLES];
    const double corner_x[5] = {x_min, x_max, x_max, x_min, x_min};
    const double corner_y[5] = {y_min, y_min, y_max, y_max, y_min};
    for (int e = 0; e < 4; e++) {
        for (int i = 0; i < FOOTPRINT_EDGE_SAMPLES; i++) {
            double t = (double)i / (double)FOOTPRINT_EDGE_SAMPLES;
            bx[e * FOOTPRINT_EDGE_SAMPLES + i] = corner_x[e] + (corner_x[e + 1] - corner_x[e]) * t;
            by[e * FOOTPRINT_EDGE_SAMPLES + i] = corner_y[e] + (corner_y[e + 1] - corner_y[e]) * t;
        }
    }

    int off_limb = 0, crossings = 0;
    for (int i = 0; i < n; i++) {
        int prev = (i + n - 1) % n;
        double lon, lat, plon, plat;
        bool ok = project_point(g, bx[i], by[i], &lon, &lat);
        bool prev_ok = project_point(g, bx[prev], by[prev], &plon, &plat);

        // On a valid/invalid transition the limb cuts this segment: nail the
        // crossing before emitting the invalid side, so the ring keeps the
        // corner instead of rounding it towards the centre. Skipping it cost
        // half a degree of latitude on a CONUS sector, whose north-west corner
        // does fall off the disk.
        if (ok != prev_ok) {
            double xlon, xlat;
            bool got = ok
                ? bisect_along_edge(g, bx[i], by[i], bx[prev], by[prev], &xlon, &xlat)
                : bisect_along_edge(g, bx[prev], by[prev], bx[i], by[i], &xlon, &xlat);
            if (got) { push(out, xlon, xlat); crossings++; }
        }

        if (!ok) {
            off_limb++;
            ok = bisect_to_limb(g, cx, cy, bx[i], by[i], &lon, &lat);
        }
        if (ok) push(out, lon, lat);
    }

    if (out->count < 4) {
        LOG_WARN("Footprint discarded: only %d valid samples on the border.", out->count);
        return 1;
    }
    ring_bbox(out, ref->proj_info.lon_origin);
    out->valid = true;
    // Not a --timing-csv stage: a fixed handful of coordinate inversions,
    // independent of the raster size, so it would never be a column worth
    // comparing between the OpenMP and CUDA builds.
    LOG_TIMING(omp_get_wtime() - t0, "Geographic footprint (%d vertices)", out->count);
    LOG_INFO("Footprint: %d vertices (%d off-limb, %d edge crossings), "
             "bbox [%.4f, %.4f, %.4f, %.4f]%s",
             out->count, off_limb, crossings, out->bbox[0], out->bbox[1],
             out->bbox[2], out->bbox[3],
             out->crosses_antimeridian ? " (crosses the antimeridian)" : "");
    return 0;
}

int footprint_from_latlon_box(double lon_min, double lat_min, double lon_max,
                              double lat_max, Footprint *out) {
    if (out == NULL) return 1;
    memset(out, 0, sizeof(*out));

    // Four vertices, not a dense walk: in EPSG:4326 the edges of this box are
    // straight lines, and GeoJSON says so too, so intermediate points carry no
    // information.
    const double bx[4] = {lon_min, lon_max, lon_max, lon_min};
    const double by[4] = {lat_min, lat_min, lat_max, lat_max};
    for (int e = 0; e < 4; e++) push(out, bx[e], by[e]);
    out->bbox[0] = lon_min; out->bbox[1] = lat_min;
    out->bbox[2] = lon_max; out->bbox[3] = lat_max;
    out->crosses_antimeridian = (lon_min > lon_max);
    out->valid = true;
    return 0;
}
