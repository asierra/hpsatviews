#!/bin/bash
set -e

# Item de STAC (-j). Sustituyó al sidecar propio en la fase 3 de
# docs/stac/STAC_PLAN.md: un solo formato, sin dos esquemas separándose.
EXPECTED_VERSION=$(grep -oE '^#define HPSV_VERSION_(MAJOR|MINOR|PATCH) [0-9]+' ../include/version.h \
    | awk '{print $3}' | paste -sd. -)

SCHEMA=../docs/stac/hpsv-item.schema.json

# Validación contra el esquema declarado. Es dependencia dura a propósito: un
# SKIP silencioso que cuenta como aprobado es justo cómo la suite CUDA llegó a
# mentir un 9/9 verde (ver CLAUDE.md). Debian/Ubuntu: python3-jsonschema.
if ! python3 -c 'import jsonschema' 2>/dev/null; then
    echo "FAIL: falta el validador de JSON Schema." >&2
    echo "      Instálalo con: sudo apt-get install python3-jsonschema" >&2
    exit 1
fi
# Las ligaduras de GDAL son la única forma de leer la geotransformación del
# GeoTIFF (va en etiquetas binarias, no la ve 'strings') y por tanto de
# comprobar que el Item describe el archivo que se escribió al lado.
if ! python3 -c 'from osgeo import gdal' 2>/dev/null; then
    echo "FAIL: faltan las ligaduras de GDAL para Python." >&2
    echo "      Instálalas con: sudo apt-get install python3-gdal" >&2
    exit 1
fi

validate() {
    local file="$1"
    if ! python3 - "$SCHEMA" "$file" <<'VALPY'
import json, sys
import jsonschema

schema = json.load(open(sys.argv[1]))
jsonschema.Draft7Validator.check_schema(schema)
instance = json.load(open(sys.argv[2]))
errors = sorted(jsonschema.Draft7Validator(schema).iter_errors(instance),
                key=lambda e: list(e.path))
for e in errors:
    path = "/".join(str(p) for p in e.path) or "(raiz)"
    print("  %s: %s" % (path, e.message), file=sys.stderr)
sys.exit(1 if errors else 0)
VALPY
    then
        echo "FAIL: $file no valida contra $SCHEMA" >&2
        exit 1
    fi
    echo "OK: $file valida contra el esquema del Item"
}

# Validación contra los esquemas OFICIALES de STAC, versionados en
# docs/stac/schemas/ por tools/fetch_stac_schemas.py. Se versionan en vez de
# descargarse para que la suite no dependa de que un servicio ajeno esté en pie
# y para que las versiones queden fijadas de forma visible en el árbol.
validate_stac() {
    local file="$1"
    if ! python3 - "$file" ../docs/stac/schemas <<'STACPY'
import json, sys
from pathlib import Path
from jsonschema import Draft7Validator, exceptions
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT7

item = json.load(open(sys.argv[1]))
root = Path(sys.argv[2])
if not root.is_dir():
    sys.exit("faltan los esquemas versionados: corre tools/fetch_stac_schemas.py")

resources = {}
for path in root.rglob("*.json"):
    doc = json.loads(path.read_text())
    rel = path.relative_to(root).as_posix()
    res = Resource.from_contents(doc, default_specification=DRAFT7)
    for scheme in ("https", "http"):
        resources[f"{scheme}://{rel}"] = res
registry = Registry().with_resources(resources.items())

def schema_for(uri):
    for key in (uri, uri.replace("https://", "http://")):
        if key in resources:
            return json.loads((root / key.split("://", 1)[1]).read_text())
    sys.exit(f"no está versionado el esquema {uri}; corre tools/fetch_stac_schemas.py")

urls = ["https://schemas.stacspec.org/v%s/item-spec/json-schema/item.json" % item["stac_version"]]
urls += item["stac_extensions"]
failed = False
for url in urls:
    errors = list(Draft7Validator(schema_for(url), registry=registry).iter_errors(item))
    if errors:
        failed = True
        print("  %s: %s" % (url, exceptions.best_match(iter(errors)).message[:160]), file=sys.stderr)
sys.exit(1 if failed else 0)
STACPY
    then
        echo "FAIL: $file no valida contra los esquemas oficiales de STAC" >&2
        exit 1
    fi
    echo "OK: $file valida contra los esquemas oficiales de STAC"
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

# El tipo de medio del activo. No se puede con check_key: su grep se queda con
# el primer "type", que es el "Feature" de la raíz.
check_asset_type() {
    local file="$1" expected="$2"
    if ! python3 - "$file" "$expected" <<'ATPY'
import json, sys
d = json.load(open(sys.argv[1]))
types = [a.get("type", "") for a in d["assets"].values()]
if not any(sys.argv[2] in t for t in types):
    sys.exit("ningun activo tiene tipo '%s'; hay %s" % (sys.argv[2], types))
ATPY
    then
        echo "FAIL: $file tipo de medio del activo" >&2
        exit 1
    fi
    echo "OK: $file activo de tipo '$expected'"
}

# Coherencia interna del Item: anillo cerrado, bbox que es el del anillo,
# activos que existen en disco, y proj:transform/proj:shape que describen el
# archivo realmente escrito.
check_item() {
    local file="$1" what="$2"
    if ! python3 - "$file" <<'ITEMPY'
import json, os, sys
from osgeo import gdal

gdal.UseExceptions()
d = json.load(open(sys.argv[1]))
base = os.path.dirname(os.path.abspath(sys.argv[1]))

ring = d["geometry"]["coordinates"][0]
if ring[0] != ring[-1]:
    sys.exit("el anillo no esta cerrado")
w, s_, e, n = d["bbox"]
if not (-90 <= s_ < n <= 90):
    sys.exit("latitudes fuera de rango o invertidas: %s %s" % (s_, n))
if w < e:   # sin cruce del antimeridiano el bbox es el del anillo
    lons = [p[0] for p in ring]; lats = [p[1] for p in ring]
    for got, want, name in ((w, min(lons), "W"), (e, max(lons), "E"),
                            (s_, min(lats), "S"), (n, max(lats), "N")):
        if abs(got - want) > 1e-6:
            sys.exit("bbox %s=%s no coincide con el anillo (%s)" % (name, got, want))

if not d["assets"]:
    sys.exit("el Item no declara ningun activo")
for key, a in d["assets"].items():
    path = os.path.join(base, a["href"])
    if not os.path.exists(path):
        sys.exit("el activo '%s' apunta a %s, que no existe" % (key, a["href"]))
    if not a["href"].endswith((".tif", ".tiff")):
        continue
    ds = gdal.Open(path)
    if a.get("proj:shape") != [ds.RasterYSize, ds.RasterXSize]:
        sys.exit("proj:shape de '%s' es %s, el archivo es %dx%d"
                 % (key, a.get("proj:shape"), ds.RasterXSize, ds.RasterYSize))
    # Cada activo carga SU transformacion: con -B los dos rasteres estan en
    # rejillas distintas y la del Item solo puede describir uno.
    if "proj:transform" not in a:
        sys.exit("el activo '%s' no declara proj:transform" % key)
    # Y su CRS. El activo de rejilla fija no tiene codigo EPSG, asi que sin
    # wkt2 propio hereda el del Item: con -B eso es WGS 84, y su origen en
    # metros se leeria como grados.
    if "proj:wkt2" not in a:
        sys.exit("el activo '%s' no declara proj:wkt2" % key)
    metres = abs(a["proj:transform"][2]) > 1000.0
    geographic = a["proj:wkt2"].startswith("GEOGCRS")
    if metres and geographic:
        sys.exit("el activo '%s' declara un CRS geografico con origen en metros (%s)"
                 % (key, a["proj:transform"][2]))
    if not metres and not geographic:
        sys.exit("el activo '%s' declara un CRS proyectado con origen en grados" % key)
    gt = ds.GetGeoTransform()
    want = [gt[1], gt[2], gt[0], gt[4], gt[5], gt[3]]
    worst = max(abs(x - y) for x, y in zip(want, a["proj:transform"]))
    if worst > 1e-3:
        sys.exit("proj:transform de '%s' no coincide con %s (%g)\n  esperado %s\n  emitido  %s"
                 % (key, a["href"], worst, want, a["proj:transform"]))

p = d["properties"]
if "proj:transform" in p:
    # El Item describe la ultima salida escrita, que con -B es la geografica.
    key = "image_geographic" if "image_geographic" in d["assets"] else "image"
    href = d["assets"][key]["href"]
    if href.endswith((".tif", ".tiff")):
        gt = gdal.Open(os.path.join(base, href)).GetGeoTransform()
        want = [gt[1], gt[2], gt[0], gt[4], gt[5], gt[3]]
        worst = max(abs(x - y) for x, y in zip(want, p["proj:transform"]))
        if worst > 1e-3:
            sys.exit("proj:transform no coincide con %s (%g)" % (href, worst))
        if p["proj:shape"] != [gdal.Open(os.path.join(base, href)).RasterYSize,
                               gdal.Open(os.path.join(base, href)).RasterXSize]:
            sys.exit("proj:shape no coincide con %s" % href)
ITEMPY
    then
        echo "FAIL: $file Item incoherente ($what)" >&2
        exit 1
    fi
    echo "OK: $file coherente ($what)"
}

C01=../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc
C13=../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

rm -f hpsv_G16_conus_*.json

# gray, PNG sin georreferencia: el Item sale igual, porque la huella no
# necesita ni reproyeccion ni rejilla de navegacion.
../bin/hpsv gray -v -s -4 -j "$C01" -o gray_json_out.png
GRAY_ITEM=hpsv_G16_conus_2024220_1302_gray_C01.json
check_key $GRAY_ITEM "type" "Feature"
check_key $GRAY_ITEM "id" "hpsv_G16_conus_2024220_1302_gray_C01"
check_key $GRAY_ITEM "platform" "goes-16"
check_key $GRAY_ITEM "hpsatviews" "$EXPECTED_VERSION"
check_key $GRAY_ITEM "hpsv:command" "gray"
check_item $GRAY_ITEM "gray PNG sin georreferencia"
validate $GRAY_ITEM
validate_stac $GRAY_ITEM

# El id NO lleva los realces: dos renderizaciones de una escena son un item.
../bin/hpsv gray -v -s -4 -j --clahe -g 1.5 "$C01" -o gray_clahe_out.png
if [ ! -f $GRAY_ITEM ]; then
    echo "FAIL: una corrida con realces genero un id distinto" >&2; exit 1
fi
echo "OK: el id no cambia con --clahe ni -g"

# --stac-collection es opcional; sin ella el Item no lleva collection.
if grep -q '"collection"' $GRAY_ITEM; then
    echo "FAIL: hay collection sin pasar --stac-collection" >&2; exit 1
fi
echo "OK: sin --stac-collection el Item no declara collection"

../bin/hpsv gray -v -s -4 -j --stac-collection goes16-abi-gray "$C01" -o gray_coll_out.png
check_key $GRAY_ITEM "collection" "goes16-abi-gray"

# pseudocolor: paleta interna y paleta de archivo.
../bin/hpsv pseudocolor -v -s -4 -j "$C13" -o pseudo_json_out.png
check_key hpsv_G16_conus_2024220_1302_pseudo_C13.json "palette" "rainbow"
validate hpsv_G16_conus_2024220_1302_pseudo_C13.json

# GeoTIFF: tipo de medio del activo, y proj:* contra el archivo real.
../bin/hpsv gray -v -s -4 -j -t "$C13" -o geom_fixed_json_out.tif
FIXED_ITEM=hpsv_G16_conus_2024220_1302_gray_C13.json
check_asset_type $FIXED_ITEM "image/tiff; application=geotiff"
python3 - <<'EOBPY' || exit 1
import json, sys
d = json.load(open("hpsv_G16_conus_2024220_1302_gray_C13.json"))
bands = d["assets"]["image"].get("eo:bands")
if not bands or bands[0]["name"] != "C13":
    sys.exit("FAIL: el activo de un gray C13 deberia declarar eo:bands [C13], tiene %s" % bands)
if "center_wavelength" not in bands[0]:
    sys.exit("FAIL: eo:bands sin center_wavelength")
print("OK: el activo de un gray C13 declara eo:bands con longitud de onda")
EOBPY
check_item $FIXED_ITEM "gray GeoTIFF rejilla fija"
validate $FIXED_ITEM
validate_stac $FIXED_ITEM

# --cog tiene que cambiar el tipo de medio, no sólo el archivo.
../bin/hpsv gray -v -s -4 -j -t --cog "$C13" -o cog_json_out.tif
check_asset_type $FIXED_ITEM "profile=cloud-optimized"

# Reproyectado: proj:epsg 4326 y la huella sigue siendo la curva del limbo.
../bin/hpsv gray -v -s -4 -j -G "$C13" -o geom_geo_json_out.png
check_key $FIXED_ITEM "proj:epsg" "4326"
check_item $FIXED_ITEM "gray reproyectado"
validate $FIXED_ITEM
validate_stac $FIXED_ITEM

# -B: dos activos en un solo Item, que es la razon de ser de D1.
../bin/hpsv gray -v -s -4 -j -t -B "$C13" -o both_json_out.tif
python3 - <<'BOTHPY' || exit 1
import json, sys
d = json.load(open("hpsv_G16_conus_2024220_1302_gray_C13.json"))
keys = sorted(d["assets"])
if keys != ["image", "image_geographic"]:
    sys.exit("FAIL: -B deberia dejar dos activos, hay %s" % keys)
print("OK: -B deja un Item con dos activos")
BOTHPY
check_item $FIXED_ITEM "gray -B"
validate $FIXED_ITEM
validate_stac $FIXED_ITEM

# rgb: command, modo, y eo:bands con los TRES canales del compuesto.
../bin/hpsv rgb -v -m ash -s -4 -j "$C13" -o rgb_json_out.png
RGB_ITEM=hpsv_G16_conus_2024220_1302_ash.json
check_key $RGB_ITEM "hpsv:command" "rgb"
check_key $RGB_ITEM "mode" "ash"
python3 - <<'BANDSPY' || exit 1
import json, sys
d = json.load(open("hpsv_G16_conus_2024220_1302_ash.json"))
# eo v1.1.0 exige eo:bands en el activo. Y un compuesto RGB no CONTIENE las
# bandas de las que salio: tres planos derivados no son cuatro canales de ABI,
# asi que este Item no declara eo en absoluto.
if "eo:bands" in d["properties"]:
    sys.exit("FAIL: eo:bands en properties; la procedencia va en hpsv:channels")
for key, a in d["assets"].items():
    if "eo:bands" in a:
        sys.exit("FAIL: el activo '%s' de un rgb declara eo:bands" % key)
if any("/eo/" in e for e in d["stac_extensions"]):
    sys.exit("FAIL: un rgb declara la extension eo sin emitir eo:bands")
names = [c["name"] for c in d["properties"]["hpsv:channels"]]
if len(names) < 3:
    sys.exit("FAIL: hpsv:channels deberia listar los canales del compuesto, lista %s" % names)
print("OK: rgb sin eo:bands; la procedencia esta en hpsv:channels %s" % names)
BANDSPY
check_item $RGB_ITEM "rgb ash"
validate $RGB_ITEM
validate_stac $RGB_ITEM

# Sin -j: no debe generarse Item (opt-in).
rm -f hpsv_G16_conus_2024220_1302_gray_C01.json
../bin/hpsv gray -v -s -4 "$C01" -o no_json_out.png
if [ -f hpsv_G16_conus_2024220_1302_gray_C01.json ]; then
    echo "FAIL: se genero el Item sin pasar -j" >&2
    exit 1
fi
echo "OK: sin -j no se genera Item"
