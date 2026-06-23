#!/bin/bash
set -e

# Sidecar JSON (-j): opt-in, validamos claves clave y que la versión coincida
# con include/version.h (single source of truth) en lugar de un valor fijo.
EXPECTED_VERSION=$(grep -oE '^#define HPSV_VERSION_(MAJOR|MINOR|PATCH) [0-9]+' ../include/version.h \
    | awk '{print $3}' | paste -sd. -)

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

# gray
../bin/hpsv gray -v -s -4 -j ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o gray_json_out.png
check_key gray_json_out.json "tool" "hpsatviews"
check_key gray_json_out.json "version" "$EXPECTED_VERSION"
check_key gray_json_out.json "satellite" "G16"
check_key gray_json_out.json "command" "gray"
check_key gray_json_out.json "output_width" "1250"

# pseudocolor con paleta interna (sin -p): debe registrar "rainbow"
../bin/hpsv pseudocolor -v -s -4 -j ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o pseudo_json_out.png
check_key pseudo_json_out.json "palette" "rainbow"

# pseudocolor con -p: debe registrar la ruta del archivo de paleta
../bin/hpsv pseudocolor -v -s -4 -j -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o pseudo_phase_json_out.png
check_key pseudo_phase_json_out.json "palette" "phase.cpt"

# Sin -j: no debe generarse sidecar (opt-in).
rm -f no_json_out.json
../bin/hpsv gray -v -s -4 ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o no_json_out.png
if [ -f no_json_out.json ]; then
    echo "FAIL: se generó no_json_out.json sin pasar -j" >&2
    exit 1
fi
echo "OK: sin -j no se genera sidecar JSON"
