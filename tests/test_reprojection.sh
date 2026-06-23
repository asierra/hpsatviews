#!/bin/bash
set -e

# Reproyección fixed-grid -> geográficas (-G). Cubre la regresión donde las
# esquinas fuera del disco visible se llenaban con 0 en vez de NonData
# (ver commit "Corregido bug que ponia 0 en lugar de NonData en las esquinas...").

check_corner_pixel() {
    local file="$1" expected="$2"
    local actual
    actual=$(identify -format "%[pixel:p{0,0}]" "$file")
    if [ "$actual" != "$expected" ]; then
        echo "FAIL: esquina (0,0) de $file = '$actual', esperado '$expected'" >&2
        exit 1
    fi
    echo "OK: esquina (0,0) de $file = $actual"
}

# Gray con alpha: la esquina debe ser transparente (graya(0,0)), no negro opaco.
../bin/hpsv gray -v -s -4 -G -a ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o gray_geo_alpha_out.png
./compare_image.sh gray_geo_alpha_out.png expected_output/ref_gray_geo_alpha.png
check_corner_pixel gray_geo_alpha_out.png "graya(0,0)"

# Pseudocolor con paleta que define color 'N' (NonData): la esquina debe usar
# ese color reservado, no el índice 0 de la paleta.
../bin/hpsv pseudocolor -v -s -4 -G -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o pseudo_geo_phase_out.png
./compare_image.sh pseudo_geo_phase_out.png expected_output/ref_pseudo_geo_phase.png
check_corner_pixel pseudo_geo_phase_out.png "srgb(255,0,0)"
