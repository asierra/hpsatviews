#!/bin/bash
set -e

# GeoTIFF pseudocolor con paleta de archivo: ejercita write_geotiff_indexed
# y la metadata GDAL embebida (colormap_min/max/size, product, satellite).
../bin/hpsv pseudocolor -v -s -4 -t -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o geo_pseudo_out.tif
./compare_image.sh geo_pseudo_out.tif expected_output/ref_pseudo_phase.tif

# Metadata GDAL embebida (sin nueva dependencia: GDALSetMetadataItem la escribe
# como texto XML plano dentro del tag GDAL_METADATA, legible con strings).
META=$(strings geo_pseudo_out.tif)

check_meta() {
    local key="$1" expected="$2"
    local line
    line=$(echo "$META" | grep "name=\"$key\"" || true)
    if [[ "$line" != *">$expected<"* ]]; then
        echo "FAIL: metadata '$key' esperado '$expected', encontrado: '$line'" >&2
        exit 1
    fi
    echo "OK: metadata $key=$expected"
}

check_meta "product" "CMIP"
check_meta "satellite" "G16"
check_meta "band" "C13"
check_meta "colormap_min" "1"
check_meta "colormap_max" "5"
check_meta "colormap_size" "6"
