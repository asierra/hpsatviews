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

# Rejilla fija con geometría (-t carga navegación): bounds en METROS y crs de la casa.
../bin/hpsv gray -v -s -4 -j -t "$C13" -o geom_fixed_json_out.tif
check_key geom_fixed_json_out.json "crs" "goes16"
grep -q '"bounds"' geom_fixed_json_out.json || { echo "FAIL: falta bounds con -t" >&2; exit 1; }
validate geom_fixed_json_out.json

# Reproyectado (-G): crs EPSG:4326 y bounds en grados.
../bin/hpsv gray -v -s -4 -j -G "$C13" -o geom_geo_json_out.png
check_key geom_geo_json_out.json "crs" "EPSG:4326"
validate geom_geo_json_out.json

# rgb: regresión de metadata_set_command(), que sólo se llamaba desde
# processing.c, así que los sidecars de rgb salían sin "command" (y el nombre
# autogenerado se quedaba en el literal "output").
../bin/hpsv rgb -v -m ash -s -4 -j "$C13" -o rgb_json_out.png
check_key rgb_json_out.json "command" "rgb"
check_key rgb_json_out.json "mode" "ash"
validate rgb_json_out.json

# Sin -j: no debe generarse sidecar (opt-in).
rm -f no_json_out.json
../bin/hpsv gray -v -s -4 "$C01" -o no_json_out.png
if [ -f no_json_out.json ]; then
    echo "FAIL: se generó no_json_out.json sin pasar -j" >&2
    exit 1
fi
echo "OK: sin -j no se genera sidecar JSON"
