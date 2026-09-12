#!/bin/bash
# Validadores de Items de STAC, compartidos por test_json.sh (lo que emite hpsv)
# y test_sweep.sh (lo que reconstruye tools/stac_sweep.py). Se sacaron aquí para
# que las dos suites validen con el mismo rasero: si divergieran, una podría
# aprobar un Item que la otra rechaza.
# Se espera que el llamador corra desde tests/.

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

