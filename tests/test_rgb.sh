#!/bin/bash
set -e

# Verifica que la imagen no sea plana/vacía (catch de salidas en negro o de un
# solo color, sin mantener una referencia exacta por cada modo).
check_nonblank() {
    local file="$1"
    local stddev
    stddev=$(identify -format "%[standard-deviation]" "$file" 2>/dev/null | head -1)
    if [ -z "$stddev" ] || awk -v s="$stddev" 'BEGIN { exit !(s <= 0) }'; then
        echo "FAIL: $file parece una imagen plana/vacía (stddev=$stddev)" >&2
        exit 1
    fi
    echo "OK: $file no está en blanco (stddev=$stddev)"
}

# Reference (truecolor): única variante con comparación pixel a pixel exacta.
../bin/hpsv rgb -m truecolor -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o "truecolor_reference.png"
./compare_image.sh truecolor_reference.png expected_output/ref_truecolor.png

# Rayleigh LUTs
../bin/hpsv rgb -m truecolor --rayleigh -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o "truecolor_ray_luts.png"
check_nonblank truecolor_ray_luts.png

# Rayleigh Analytic
../bin/hpsv rgb -m truecolor --ray-analytic -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o "truecolor_ray_analytic.png"
check_nonblank truecolor_ray_analytic.png

# Otros modos: solo sanity-check (corren y producen una imagen con contenido
# real), sin referencia exacta. airmass/severestorm/so2 no se cubren aquí
# porque requieren canales (C05/C07-C10/C12) que no están en sample_data/.
../bin/hpsv rgb -m night -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o "night_out.png"
check_nonblank night_out.png

../bin/hpsv rgb -m ash -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o "ash_out.png"
check_nonblank ash_out.png

../bin/hpsv rgb -m daynite -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o "daynite_out.png"
check_nonblank daynite_out.png
