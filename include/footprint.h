/* Geographic footprint (EPSG:4326) of a raster on the satellite's fixed grid.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * STAC wants geometry and bbox in EPSG:4326 for every item, but the products
 * live on the geostationary fixed grid, where the four corners of a full disk
 * fall off the Earth entirely: the visible limb is an ellipse inscribed in the
 * raster, so transforming the corners yields nothing usable. This walks the
 * raster border instead and, whenever a sample lands off the limb, bisects it
 * towards the centre until it lands back on the Earth, so the ring follows the
 * limb across the stretches the raster does not reach. Where an edge actually
 * crosses the limb the exact intersection is bisected along the edge and
 * inserted: that corner is where the extreme latitude or longitude usually
 * sits, and rounding it off cost half a degree on a CONUS sector. One routine
 * serves full disks, sectors and clips alike.
 */
#ifndef HPSATVIEWS_FOOTPRINT_H_
#define HPSATVIEWS_FOOTPRINT_H_

#include <stdbool.h>
#include "datanc.h"

/// Samples per raster edge. On a full disk the ring follows the limb, where
/// 32 samples per edge put the vertices ~2.8° apart in satellite view angle.
#define FOOTPRINT_EDGE_SAMPLES 32
/// The border samples, plus the exact edge-limb crossings: a straight edge cuts
/// the limb ellipse at most twice, so eight extra vertices always suffice.
#define FOOTPRINT_MAX_POINTS (4 * FOOTPRINT_EDGE_SAMPLES + 8)

typedef struct {
    double lon[FOOTPRINT_MAX_POINTS];
    double lat[FOOTPRINT_MAX_POINTS];
    int count;              ///< Ring vertices, in order, NOT closed.
    double bbox[4];         ///< [W, S, E, N] in degrees.
    bool crosses_antimeridian; ///< True when bbox[0] > bbox[2], per the STAC convention.
    bool valid;
} Footprint;

/// Footprint of a raster whose extent on the fixed grid is the given box, in
/// metres. Returns 0 on success; non-zero when the projection is unknown, its
/// parameters are missing, or not one sample of the raster sees the Earth.
int footprint_from_geos_box(const DataNC *ref, double x_min, double y_min,
                            double x_max, double y_max, Footprint *out);

/// Footprint of a raster already in EPSG:4326: the rectangle itself.
int footprint_from_latlon_box(double lon_min, double lat_min, double lon_max,
                              double lat_max, Footprint *out);

#endif /* HPSATVIEWS_FOOTPRINT_H_ */
