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

# --cog toggle: the default GeoTIFF is a single-page tiled file (no overviews);
# --cog builds the full Cloud Optimized GeoTIFF with an overview pyramid (extra
# TIFF pages). identify's page count distinguishes them (stderr suppressed: the
# GeoTIFF private tags trigger harmless libtiff "Unknown field" warnings).
C13=../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc
../bin/hpsv gray -t "$C13" -o geo_default.tif
../bin/hpsv gray -t --cog "$C13" -o geo_cog.tif
n_default=$(identify -format "%n\n" geo_default.tif 2>/dev/null | head -1)
n_cog=$(identify -format "%n\n" geo_cog.tif 2>/dev/null | head -1)
if [ "$n_default" -ne 1 ]; then
    echo "FAIL: default GeoTIFF should have no overviews (pages=$n_default)" >&2; exit 1
fi
if [ "$n_cog" -le 1 ]; then
    echo "FAIL: --cog GeoTIFF should embed overviews (pages=$n_cog)" >&2; exit 1
fi
echo "OK: default sin overviews (1 pág), --cog con overviews ($n_cog págs)"
