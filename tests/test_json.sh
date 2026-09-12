#!/bin/bash
set -e

# Sidecar JSON (-j): opt-in, validamos claves clave y que la versión coincida
# con include/version.h (single source of truth) en lugar de un valor fijo.
EXPECTED_VERSION=$(grep -oE '^#define HPSV_VERSION_(MAJOR|MINOR|PATCH) [0-9]+' ../include/version.h \
    | awk '{print $3}' | paste -sd. -)

SCHEMA=../docs/hpsatviews.schema.json

# Validación contra el esquema declarado. Es dependencia dura a propósito: un
# SKIP silencioso que cuenta como aprobado es justo cómo la suite CUDA llegó a
# mentir un 9/9 verde (ver CLAUDE.md). Debian/Ubuntu: python3-jsonschema.
if ! python3 -c 'import jsonschema' 2>/dev/null; then
    echo "FAIL: falta el validador de JSON Schema." >&2
    echo "      Instálalo con: sudo apt-get install python3-jsonschema" >&2
    echo "      (o pip install jsonschema)" >&2
    exit 1
fi
# Las ligaduras de GDAL son la única forma de leer la geotransformación del
# GeoTIFF (va en etiquetas binarias, no la ve 'strings') y por tanto de
# comprobar que el sidecar describe el archivo que se escribió al lado.
if ! python3 -c 'from osgeo import gdal' 2>/dev/null; then
    echo "FAIL: faltan las ligaduras de GDAL para Python." >&2
    echo "      Instálalas con: sudo apt-get install python3-gdal" >&2
    exit 1
fi

validate() {
    local file="$1"
    if ! python3 - "$SCHEMA" "$file" <<'PY'
import json, sys
import jsonschema

schema = json.load(open(sys.argv[1]))
jsonschema.Draft7Validator.check_schema(schema)
instance = json.load(open(sys.argv[2]))
errors = sorted(jsonschema.Draft7Validator(schema).iter_errors(instance),
                key=lambda e: list(e.path))
for e in errors:
    path = "/".join(str(p) for p in e.path) or "(raíz)"
    print(f"  {path}: {e.message}", file=sys.stderr)
sys.exit(1 if errors else 0)
PY
    then
        echo "FAIL: $file no valida contra $SCHEMA" >&2
        exit 1
    fi
    echo "OK: $file valida contra el esquema"
}

check_key() {
    local file="$1" key="$2" expected="$3"
    local line
    line=$(grep "\"$key\"" "$file" | head -1)
    if [[ "$line" != *"$expected"* ]]; then
        echo "FAIL: $file clave '$key' esperado '$expected', encontrado: '$line'" >&2
        exit 1
    fi
    echo "OK: $file $key=$expected"
}

C01=../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc
C13=../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

# gray
../bin/hpsv gray -v -s -4 -j "$C01" -o gray_json_out.png
check_key gray_json_out.json "tool" "hpsatviews"
check_key gray_json_out.json "version" "$EXPECTED_VERSION"
check_key gray_json_out.json "satellite" "G16"
check_key gray_json_out.json "command" "gray"
check_key gray_json_out.json "output_width" "1250"
validate gray_json_out.json

# pseudocolor con paleta interna (sin -p): debe registrar "rainbow"
../bin/hpsv pseudocolor -v -s -4 -j "$C13" -o pseudo_json_out.png
check_key pseudo_json_out.json "palette" "rainbow"
validate pseudo_json_out.json

# pseudocolor con -p: debe registrar la ruta del archivo de paleta
../bin/hpsv pseudocolor -v -s -4 -j -p ../assets/phase.cpt "$C13" -o pseudo_phase_json_out.png
check_key pseudo_phase_json_out.json "palette" "phase.cpt"
validate pseudo_phase_json_out.json

# Huella geográfica (EPSG:4326). Se emite siempre, incluso en el PNG pelado que
# no lleva `bounds` ni `geometry`, y en la rejilla fija sigue el limbo en vez de
# las esquinas del ráster.
check_footprint() {
    local file="$1" what="$2"
    if ! python3 - "$file" "$what" <<'FPPY'
import json, sys

d = json.load(open(sys.argv[1]))
bbox = d.get("bbox_4326")
fp = d.get("footprint")
if bbox is None or fp is None:
    sys.exit("falta bbox_4326 o footprint")
ring = fp["coordinates"][0]
if fp.get("type") != "Polygon" or len(ring) < 5:
    sys.exit("anillo degenerado: %d vértices" % len(ring))
if ring[0] != ring[-1]:
    sys.exit("el anillo no está cerrado")
w, s_, e, n = bbox
if not (-90 <= s_ < n <= 90):
    sys.exit("latitudes fuera de rango o invertidas: %s %s" % (s_, n))
for lon, lat in ring:
    if not (-180 <= lon <= 180 and -90 <= lat <= 90):
        sys.exit("vértice fuera de rango: %s %s" % (lon, lat))
# El bbox tiene que ser el del anillo. Si cruza el antimeridiano (W>E) la
# comparación directa no aplica, así que sólo se verifica en el caso normal.
if w < e:
    lons = [p[0] for p in ring]
    lats = [p[1] for p in ring]
    for got, want, name in ((w, min(lons), "W"), (e, max(lons), "E"),
                            (s_, min(lats), "S"), (n, max(lats), "N")):
        if abs(got - want) > 1e-6:
            sys.exit("bbox %s=%s no coincide con el anillo (%s)" % (name, got, want))
FPPY
    then
        echo "FAIL: $file huella inválida ($what)" >&2
        exit 1
    fi
    echo "OK: $file huella coherente ($what)"
}

check_footprint gray_json_out.json "PNG sin geometria"

# Rejilla de salida (proj_transform/proj_shape/proj_epsg/proj_wkt2). Tiene que
# describir el archivo que se escribió al lado, no la imagen antes de escalar:
# en rgb la rejilla se registra después del remuestreo por esa razón.
check_grid() {
    local json="$1" tif="$2" what="$3"
    if ! python3 - "$json" "$tif" <<'GRIDPY'
import json, sys
from osgeo import gdal

gdal.UseExceptions()
d = json.load(open(sys.argv[1]))
for k in ("proj_transform", "proj_shape", "proj_wkt2"):
    if k not in d:
        sys.exit("falta " + k)
ds = gdal.Open(sys.argv[2])
gt = ds.GetGeoTransform()
want = [gt[1], gt[2], gt[0], gt[4], gt[5], gt[3]]   # orden STAC, no el de GDAL
got = d["proj_transform"]
worst = max(abs(a - b) for a, b in zip(want, got))
if worst > 1e-3:
    sys.exit("proj_transform no coincide con el GeoTIFF (%g)\n  esperado %s\n  emitido  %s"
             % (worst, want, got))
if d["proj_shape"] != [ds.RasterYSize, ds.RasterXSize]:
    sys.exit("proj_shape %s no es [alto, ancho] del archivo (%d x %d)"
             % (d["proj_shape"], ds.RasterXSize, ds.RasterYSize))
if not d["proj_wkt2"].startswith(("PROJCRS", "GEOGCRS")):
    sys.exit("proj_wkt2 no parece WKT2: " + d["proj_wkt2"][:40])
GRIDPY
    then
        echo "FAIL: $json rejilla incoherente ($what)" >&2
        exit 1
    fi
    echo "OK: $json rejilla coincide con el GeoTIFF ($what)"
}

# Rejilla fija con geometría (-t carga navegación): bounds en METROS y crs de la casa.
../bin/hpsv gray -v -s -4 -j -t "$C13" -o geom_fixed_json_out.tif
check_key geom_fixed_json_out.json "crs" "goes16"
check_grid geom_fixed_json_out.json geom_fixed_json_out.tif "gray rejilla fija, -s -4"
grep -q '"bounds"' geom_fixed_json_out.json || { echo "FAIL: falta bounds con -t" >&2; exit 1; }
validate geom_fixed_json_out.json

# Reproyectado (-G): crs EPSG:4326 y bounds en grados.
../bin/hpsv gray -v -s -4 -j -G "$C13" -o geom_geo_json_out.png
check_key geom_geo_json_out.json "crs" "EPSG:4326"
check_footprint geom_geo_json_out.json "reproyectado"
validate geom_geo_json_out.json

# La extensión en metros no puede depender de -s: gt[1] es el tamaño de píxel
# SIN escalar, y multiplicarlo por el ancho ya reducido daba un cuarto de la
# extensión real, contradiciendo la geotransformación del propio GeoTIFF.
../bin/hpsv gray -v -j -t "$C13" -o extent_full_json_out.tif
../bin/hpsv gray -v -j -t -s -4 "$C13" -o extent_scaled_json_out.tif
python3 - <<'EXPY' || exit 1
import json, sys
a = json.load(open("extent_full_json_out.json"))["bounds"]
b = json.load(open("extent_scaled_json_out.json"))["bounds"]
if max(abs(x - y) for x, y in zip(a, b)) > 1.0:
    sys.exit("FAIL: -s cambio la extension en metros\n  sin -s: %s\n  con -s: %s" % (a, b))
print("OK: la extension en metros no depende de -s")
EXPY
check_grid extent_scaled_json_out.json extent_scaled_json_out.tif "escalado"

# rgb reproyectado: el remuestreo ocurre despues de registrar la geometria, asi
# que proj_shape tiene que seguir siendo el del archivo, no el de antes.
../bin/hpsv rgb -v -m ash -j -t -G -s -4 "$C13" -o rgb_geo_json_out.tif
check_key rgb_geo_json_out.json "proj_epsg" "4326"
check_grid rgb_geo_json_out.json rgb_geo_json_out.tif "rgb reproyectado, -s -4"
validate rgb_geo_json_out.json

# rgb: regresión de metadata_set_command(), que sólo se llamaba desde
# processing.c, así que los sidecars de rgb salían sin "command" (y el nombre
# autogenerado se quedaba en el literal "output").
../bin/hpsv rgb -v -m ash -s -4 -j "$C13" -o rgb_json_out.png
check_key rgb_json_out.json "command" "rgb"
check_key rgb_json_out.json "mode" "ash"
check_footprint rgb_json_out.json "rgb rejilla fija"
validate rgb_json_out.json

# Sin -j: no debe generarse sidecar (opt-in).
rm -f no_json_out.json
../bin/hpsv gray -v -s -4 "$C01" -o no_json_out.png
if [ -f no_json_out.json ]; then
    echo "FAIL: se generó no_json_out.json sin pasar -j" >&2
    exit 1
fi
echo "OK: sin -j no se genera sidecar JSON"
