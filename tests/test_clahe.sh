#!/bin/bash
set -e

# Gray reference
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -i

../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc

# Gray CLAHE
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -i --clahe

../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc --clahe

# RGB reference
../bin/hpsv rgb -m truecolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc

# RGB CLAHE
../bin/hpsv rgb -m truecolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc --clahe

