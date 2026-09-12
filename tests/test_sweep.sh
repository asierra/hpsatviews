#!/bin/bash
set -e

# Barrido retroactivo (tools/stac_sweep.py): reconstruye Items de STAC desde
# GeoTIFF ya producidos, para el acervo escrito antes de que hpsv -j los
# emitiera.
#
# La prueba que importa es la CRUZADA: sobre los mismos archivos, el Item
# reconstruido tiene que coincidir con el que emite hpsv. El barrido lleva su
# propio port de la huella de src/footprint.c -- dos implementaciones del mismo
# algoritmo --, y esto es lo único que impide que se separen en silencio.

source ./stac_validators.sh

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

C13=../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

# -B deja dos rásteres en rejillas distintas: es el caso que obliga al barrido a
# agruparlos en UN Item con dos activos, como hace el emisor.
../bin/hpsv gray -t -B -s -4 -j "$C13" -o "$WORK/ref.tif" >/dev/null 2>&1
mv "$WORK"/hpsv_*.json "$WORK/emitido.json"

python3 ../tools/stac_sweep.py "$WORK" >/dev/null
BARRIDO=$(ls "$WORK"/hpsv_*.json)
echo "OK: el barrido reconstruyó $(basename "$BARRIDO")"

python3 - "$WORK/emitido.json" "$BARRIDO" <<'CMPPY' || exit 1
import json, sys

a = json.load(open(sys.argv[1]))
b = json.load(open(sys.argv[2]))

if a["id"] != b["id"]:
    sys.exit("FAIL: id distinto: %s vs %s" % (a["id"], b["id"]))
if sorted(a["assets"]) != sorted(b["assets"]):
    sys.exit("FAIL: activos distintos: %s vs %s" % (sorted(a["assets"]), sorted(b["assets"])))
if a["stac_extensions"] != b["stac_extensions"]:
    sys.exit("FAIL: extensiones distintas")
if a["properties"]["datetime"] != b["properties"]["datetime"]:
    sys.exit("FAIL: datetime distinto")

# El anillo se compara vertice a vertice, no como conjunto: el barrido recorre
# el borde desde la misma esquina y en el mismo sentido que src/footprint.c a
# proposito, justo para que esta comparacion sea estricta. La tolerancia cubre
# la diferencia de elipsoide entre leer el NetCDF y leer el CRS del GeoTIFF.
TOL = 1e-4
ra = a["geometry"]["coordinates"][0]
rb = b["geometry"]["coordinates"][0]
if len(ra) != len(rb):
    sys.exit("FAIL: el anillo tiene %d vertices emitido y %d reconstruido" % (len(ra), len(rb)))
worst = max(max(abs(p[0] - q[0]), abs(p[1] - q[1])) for p, q in zip(ra, rb))
if worst > TOL:
    sys.exit("FAIL: el anillo difiere hasta %g grados; las dos huellas se separaron" % worst)

if max(abs(x - y) for x, y in zip(a["bbox"], b["bbox"])) > TOL:
    sys.exit("FAIL: bbox distinto")

for key in ("proj:shape", "proj:epsg"):
    if a["properties"].get(key) != b["properties"].get(key):
        sys.exit("FAIL: %s distinto: %s vs %s" % (key, a["properties"].get(key), b["properties"].get(key)))
if max(abs(x - y) for x, y in zip(a["properties"]["proj:transform"],
                                  b["properties"]["proj:transform"])) > 1e-3:
    sys.exit("FAIL: proj:transform distinto")

for key in sorted(a["assets"]):
    x, y = a["assets"][key], b["assets"][key]
    if x["href"] != y["href"] or x["proj:shape"] != y["proj:shape"]:
        sys.exit("FAIL: el activo '%s' no coincide" % key)
    if max(abs(i - j) for i, j in zip(x["proj:transform"], y["proj:transform"])) > 1e-3:
        sys.exit("FAIL: proj:transform del activo '%s' no coincide" % key)

print("OK: el Item reconstruido coincide con el emitido (anillo hasta %.1e grados)" % worst)
CMPPY

# Lo que el barrido NO puede recuperar tiene que declararlo, no inventarlo.
python3 - "$BARRIDO" <<'MISSPY' || exit 1
import json, sys
d = json.load(open(sys.argv[1]))
p = d["properties"]
if not p.get("hpsv:reconstructed"):
    sys.exit("FAIL: un Item reconstruido no se declara como tal")
for key in ("hpsv:channels", "hpsv:enhancements"):
    if key in p:
        sys.exit("FAIL: %s no se puede reconstruir desde un GeoTIFF y aqui aparece" % key)
print("OK: declara hpsv:reconstructed y no inventa channels ni enhancements")
MISSPY

validate "$BARRIDO"
validate_stac "$BARRIDO"

# Un archivo que no escribió hpsv no se toca.
python3 -c "
from osgeo import gdal; gdal.UseExceptions()
gdal.GetDriverByName('GTiff').Create('$WORK/ajeno.tif', 4, 4, 1)" 
rm -f "$WORK"/hpsv_*.json
if python3 ../tools/stac_sweep.py "$WORK/ajeno.tif" 2>&1 | grep -q "no lo escribió hpsv"; then
    echo "OK: un GeoTIFF ajeno se omite en vez de reconstruirse a medias"
else
    echo "FAIL: el barrido no omitió un GeoTIFF que no escribió hpsv" >&2
    exit 1
fi
