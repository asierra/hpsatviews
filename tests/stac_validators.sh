#!/bin/bash
# Validadores de Items de STAC, compartidos por test_json.sh (lo que emite hpsv)
# y test_sweep.sh (lo que reconstruye tools/stac_sweep.py). Se sacaron aquí para
# que las dos suites validen con el mismo rasero: si divergieran, una podría
# aprobar un Item que la otra rechaza.
# Se espera que el llamador corra desde tests/.

SCHEMA=../docs/stac/hpsv-item.schema.json

# Sugerencia de instalación según la familia de la distribución. El proyecto se
# compila tanto en Debian/Ubuntu como en RHEL/Rocky (ver CLAUDE.md), así que un
# mensaje que sólo sepa de apt manda a quien esté en Rocky a buscar un paquete
# que no existe con ese nombre. Para las ligaduras de GDAL además se imprime
# cómo averiguar el nombre, porque varía entre Fedora y EPEL y entre versiones.
install_hint() {
    local deb="$1" rpm="$2" probe="${3:-}"
    if command -v apt-get >/dev/null 2>&1; then
        echo "      Instálalo con: sudo apt-get install $deb" >&2
    elif command -v dnf >/dev/null 2>&1; then
        echo "      Instálalo con: sudo dnf install $rpm" >&2
        [[ -n "$probe" ]] && echo "      Si ese nombre no existe: dnf provides '$probe'" >&2
    elif command -v zypper >/dev/null 2>&1; then
        echo "      Instálalo con: sudo zypper install $rpm" >&2
    else
        echo "      Paquete: $deb (Debian/Ubuntu) o $rpm (RHEL/Rocky/Fedora)" >&2
    fi
}

# Validación contra el esquema declarado. Es dependencia dura a propósito: un
# SKIP silencioso que cuenta como aprobado es justo cómo la suite CUDA llegó a
# mentir un 9/9 verde (ver CLAUDE.md).
if ! python3 -c 'import jsonschema' 2>/dev/null; then
    echo "FAIL: falta el validador de JSON Schema." >&2
    install_hint python3-jsonschema python3-jsonschema
    exit 1
fi
# Las ligaduras de GDAL son la única forma de leer la geotransformación del
# GeoTIFF (va en etiquetas binarias, no la ve 'strings') y por tanto de
# comprobar que el Item describe el archivo que se escribió al lado.
if ! python3 -c 'from osgeo import gdal' 2>/dev/null; then
    echo "FAIL: faltan las ligaduras de GDAL para Python." >&2
    install_hint python3-gdal python3-gdal '*/osgeo/gdal.py'
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

item = json.load(open(sys.argv[1]))
root = Path(sys.argv[2])
if not root.is_dir():
    sys.exit("faltan los esquemas versionados: corre tools/fetch_stac_schemas.py")

docs = {}
for path in root.rglob("*.json"):
    doc = json.loads(path.read_text())
    rel = path.relative_to(root).as_posix()
    for scheme in ("https", "http"):
        docs[f"{scheme}://{rel}"] = doc

def schema_for(uri):
    for key in (uri, uri.replace("https://", "http://")):
        if key in docs:
            return docs[key]
    sys.exit(f"no está versionado el esquema {uri}; corre tools/fetch_stac_schemas.py")

# jsonschema acepta registry= (paquete referencing) solo desde 4.18; Ubuntu
# 24.04, que es el runner del CI, trae 4.10.3. Ahí se cae a RefResolver, con
# los manejadores de red anulados: un $ref que no esté versionado debe fallar,
# no descargarse en silencio.
try:
    from referencing import Registry, Resource
    from referencing.jsonschema import DRAFT7
    registry = Registry().with_resources(
        (uri, Resource.from_contents(doc, default_specification=DRAFT7))
        for uri, doc in docs.items())
    Draft7Validator({}, registry=registry)
    def validator(url):
        return Draft7Validator(schema_for(url), registry=registry)
except (ImportError, TypeError):
    from jsonschema import RefResolver
    def offline(uri):
        sys.exit(f"no está versionado el esquema {uri}; corre tools/fetch_stac_schemas.py")
    def validator(url):
        schema = schema_for(url)
        resolver = RefResolver(url, schema, store=docs,
                               handlers={"http": offline, "https": offline})
        return Draft7Validator(schema, resolver=resolver)

urls = ["https://schemas.stacspec.org/v%s/item-spec/json-schema/item.json" % item["stac_version"]]
urls += item["stac_extensions"]
failed = False
for url in urls:
    errors = list(validator(url).iter_errors(item))
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

