#!/bin/bash
set -e

# Gray reference
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -i

../bin/hpsv gray -v -s -4 ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc -o gray_out.png
./compare_image.sh gray_out.png expected_output/ref_gray.png

# Gray CLAHE
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -i --clahe

../bin/hpsv gray -v -s -4 ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc --clahe -o gray_clahe_out.png
./compare_image.sh gray_clahe_out.png expected_output/ref_gray_clahe.png

# RGB reference
../bin/hpsv rgb -m truecolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc

# RGB CLAHE
../bin/hpsv rgb -m truecolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc --clahe

