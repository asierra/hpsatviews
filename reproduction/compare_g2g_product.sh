#!/bin/bash
# Check that hpsv and geo2grid actually produce the same product, so the timings
# from bench_geo2grid.sh compare like with like.
#
# Usage:
#   reproduction/compare_g2g_product.sh <geo2grid.tif> <hpsv.tif>
#
# Env:
#   SIZE   Edge of the grid both images are averaged down to. Default 1024.
#          Full-disk pairs are ~2 GB together; comparing them at native size
#          buys nothing here and costs a lot of RAM.
#
# geo2grid writes RGBA (alpha masks the off-disk corners); hpsv writes RGB. Only
# the pixels geo2grid marks opaque are compared -- off-disk fill is not data and
# would swamp the statistics.
#
# Needs gdal_translate and python3 with osgeo+numpy.
set -eu

G2G="${1:?Usage: $0 <geo2grid.tif> <hpsv.tif>}"
HPSV="${2:?Usage: $0 <geo2grid.tif> <hpsv.tif>}"
SIZE="${SIZE:-1024}"
command -v gdal_translate >/dev/null || { echo "gdal_translate not found" >&2; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
gdal_translate -q -outsize "$SIZE" "$SIZE" -r average "$G2G"  "$TMP/a.tif"
gdal_translate -q -outsize "$SIZE" "$SIZE" -r average "$HPSV" "$TMP/b.tif"

python3 - "$TMP/a.tif" "$TMP/b.tif" <<'PY'
import sys
import numpy as np
from osgeo import gdal
gdal.UseExceptions()

a = gdal.Open(sys.argv[1]).ReadAsArray().astype(np.float64)
b = gdal.Open(sys.argv[2]).ReadAsArray().astype(np.float64)

# Mask: geo2grid's alpha if present, else every pixel either image calls non-black.
if a.shape[0] >= 4:
    m = a[3] > 250
else:
    m = (a[:3].sum(axis=0) > 0) | (b[:3].sum(axis=0) > 0)
a, b = a[:3], b[:3]
if a.shape != b.shape:
    sys.exit("Shape mismatch after resize: %s vs %s" % (a.shape, b.shape))

d = (a - b)[:, m]
print("compared      : %.1f%% of pixels (on-disk)" % (100 * m.mean()))
print("MAE           : %6.2f DN of 255" % np.abs(d).mean())
print("RMSE          : %6.2f DN" % np.sqrt((d ** 2).mean()))
print("within 10 DN  : %6.1f%%" % (100 * (np.abs(d) <= 10).mean()))
print("within 25 DN  : %6.1f%%" % (100 * (np.abs(d) <= 25).mean()))
print("p99 |diff|    : %6.2f DN" % np.percentile(np.abs(d), 99))
print("mean level    : geo2grid %.1f  hpsv %.1f" % (a[:, m].mean(), b[:, m].mean()))
for i, c in enumerate("RGB"):
    print("  %s bias (hpsv - geo2grid): %+5.2f DN" % (c, (b[i][m] - a[i][m]).mean()))

# RMSE far above MAE means a small tail of large differences -- expect it at the
# limb and the terminator, where the solar-zenith normalisation diverges. Say so
# rather than quoting the MAE alone.
if np.sqrt((d ** 2).mean()) > 3 * np.abs(d).mean():
    print("\nNOTE: RMSE >> MAE -- a minority of pixels differ a lot (check the")
    print("      limb and the day/night terminator before quoting agreement).")
PY
