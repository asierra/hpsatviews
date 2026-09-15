#!/bin/bash
# Check that hpsv and geo2grid actually produce the same product, so the timings
# from bench_geo2grid.sh compare like with like.
#
# Usage:
#   reproduction/compare_g2g_product.sh <geo2grid.tif> <hpsv.tif>
#
# Env:
#   SIZE        Edge of the grid both images are averaged down to. Default 1024.
#               Full-disk pairs are ~2 GB together; comparing them at native
#               size buys nothing here and costs a lot of RAM.
#   SZA_MAX     Solar zenith angle, in degrees, beyond which pixels are left out.
#               Default 85, which is where hpsv blanks the true-color composite
#               (MAX_SZA in src/truecolor.c); geo2grid keeps rendering to ~90.
#               Without the cut the statistics mostly measure that band rather
#               than the product, so it is reported separately below. Set
#               SZA_MAX=180 to compare the whole disk.
#   SCENE_TIME  UTC time for the solar geometry, as YYYY-MM-DDTHH:MM:SSZ.
#               Default: the scan_time tag hpsv writes into its GeoTIFF, which
#               is the time hpsv itself uses for the cut.
#
# geo2grid writes RGBA (alpha masks the off-disk corners); hpsv writes RGB. Only
# the pixels geo2grid marks opaque, and with a solar zenith below SZA_MAX, are
# compared -- off-disk fill is not data and would swamp the statistics.
#
# Needs gdal_translate and python3 with osgeo+numpy.
set -eu

G2G="${1:?Usage: $0 <geo2grid.tif> <hpsv.tif>}"
HPSV="${2:?Usage: $0 <geo2grid.tif> <hpsv.tif>}"
SIZE="${SIZE:-1024}"
export SZA_MAX="${SZA_MAX:-85}"
export SCENE_TIME="${SCENE_TIME:-}"
command -v gdal_translate >/dev/null || { echo "gdal_translate not found" >&2; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
gdal_translate -q -outsize "$SIZE" "$SIZE" -r average "$G2G"  "$TMP/a.tif"
gdal_translate -q -outsize "$SIZE" "$SIZE" -r average "$HPSV" "$TMP/b.tif"

python3 - "$TMP/a.tif" "$TMP/b.tif" <<'PY'
import os
import sys
from datetime import datetime, timezone
import numpy as np
from osgeo import gdal, osr
gdal.UseExceptions()

A = gdal.Open(sys.argv[1])
B = gdal.Open(sys.argv[2])
a = A.ReadAsArray().astype(np.float64)
b = B.ReadAsArray().astype(np.float64)

# Mask: geo2grid's alpha if present, else every pixel either image calls non-black.
if a.shape[0] >= 4:
    disk = a[3] > 250
else:
    disk = (a[:3].sum(axis=0) > 0) | (b[:3].sum(axis=0) > 0)
a, b = a[:3], b[:3]
if a.shape != b.shape:
    sys.exit("Shape mismatch after resize: %s vs %s" % (a.shape, b.shape))

# Solar zenith of every pixel centre, from the geostationary georeference of the
# geo2grid image (both tools write the same fixed grid).
stamp = os.environ["SCENE_TIME"] or B.GetMetadataItem("scan_time") or ""
if not stamp:
    sys.exit("No scene time: hpsv's GeoTIFF has no scan_time tag; set SCENE_TIME.")
t = datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)

srs = osr.SpatialReference(wkt=A.GetProjection())
proj4 = srs.ExportToProj4()
if "+proj=geos" not in proj4 or "+sweep=x" not in proj4:
    sys.exit("Expected a GOES fixed grid (+proj=geos +sweep=x), got: " + proj4)
h = srs.GetProjParm(osr.SRS_PP_SATELLITE_HEIGHT)
lon0 = np.radians(srs.GetProjParm(osr.SRS_PP_CENTRAL_MERIDIAN))
req, rpol = srs.GetSemiMajor(), srs.GetSemiMinor()
H = h + req

gt = A.GetGeoTransform()
ny, nx = disk.shape
x, y = np.meshgrid((gt[0] + gt[1] * (np.arange(nx) + 0.5)) / h,
                   (gt[3] + gt[5] * (np.arange(ny) + 0.5)) / h)
# Inverse of the fixed-grid projection (GOES-R PUG, sweep x).
qa = np.sin(x)**2 + np.cos(x)**2 * (np.cos(y)**2 + (req / rpol)**2 * np.sin(y)**2)
qb = -2 * H * np.cos(x) * np.cos(y)
qc = H**2 - req**2
disc = qb**2 - 4 * qa * qc
seen = disc >= 0
rs = (-qb - np.sqrt(np.where(seen, disc, 0))) / (2 * qa)
sx, sy, sz = rs * np.cos(x) * np.cos(y), -rs * np.sin(x), rs * np.cos(x) * np.sin(y)
lat = np.arctan((req / rpol)**2 * sz / np.sqrt((H - sx)**2 + sy**2))
lon = lon0 - np.arctan(sy / (H - sx))

# Solar position (NOAA general solar position equations; ~0.1 deg accuracy).
hr = t.hour + t.minute / 60 + t.second / 3600
g = 2 * np.pi / 365 * (t.timetuple().tm_yday - 1 + (hr - 12) / 24)
decl = (0.006918 - 0.399912 * np.cos(g) + 0.070257 * np.sin(g)
        - 0.006758 * np.cos(2 * g) + 0.000907 * np.sin(2 * g)
        - 0.002697 * np.cos(3 * g) + 0.00148 * np.sin(3 * g))
eqt = 229.18 * (0.000075 + 0.001868 * np.cos(g) - 0.032077 * np.sin(g)
                - 0.014615 * np.cos(2 * g) - 0.040849 * np.sin(2 * g))
ha = np.radians((hr * 60 + eqt + 4 * np.degrees(lon)) / 4 - 180)
cosz = np.sin(lat) * np.sin(decl) + np.cos(lat) * np.cos(decl) * np.cos(ha)
sza = np.degrees(np.arccos(np.clip(cosz, -1, 1)))

sza_max = float(os.environ["SZA_MAX"])
disk &= seen
m = disk & (sza < sza_max)
cut = disk & ~m

absd = np.abs(a - b)
d = (a - b)[:, m]
print("scene time    : %s   SZA_MAX %g deg" % (stamp, sza_max))
print("compared      : %.1f%% of on-disk pixels" % (100 * m.sum() / disk.sum()))
print("MAE           : %6.2f DN of 255 (%.1f%% of full scale)"
      % (np.abs(d).mean(), 100 * np.abs(d).mean() / 255))
print("RMSE          : %6.2f DN" % np.sqrt((d ** 2).mean()))
print("within 10 DN  : %6.1f%%" % (100 * (np.abs(d) <= 10).mean()))
print("within 25 DN  : %6.1f%%" % (100 * (np.abs(d) <= 25).mean()))
print("p99 |diff|    : %6.2f DN" % np.percentile(np.abs(d), 99))
print("mean level    : geo2grid %.1f  hpsv %.1f" % (a[:, m].mean(), b[:, m].mean()))
for i, c in enumerate("RGB"):
    print("  %s bias (hpsv - geo2grid): %+5.2f DN" % (c, (b[i][m] - a[i][m]).mean()))

if cut.any():
    print("\nleft out      : %.1f%% of on-disk pixels (SZA >= %g), carrying %.0f%% of"
          % (100 * cut.sum() / disk.sum(), sza_max,
             100 * absd[:, cut].sum() / absd[:, disk].sum()))
    print("                the whole-disk absolute difference; MAE there %.1f DN"
          % absd[:, cut].mean())

# RMSE far above MAE means a small tail of large differences -- expect it at the
# limb and the terminator, where the solar-zenith normalisation diverges. Say so
# rather than quoting the MAE alone.
if np.sqrt((d ** 2).mean()) > 3 * np.abs(d).mean():
    print("\nNOTE: RMSE >> MAE -- a minority of pixels differ a lot (check the")
    print("      limb and the day/night terminator before quoting agreement).")
PY
