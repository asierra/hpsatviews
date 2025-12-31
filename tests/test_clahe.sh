#!/bin/bash
set -e

# Gray reference
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc -i

../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc

# Gray CLAHE
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc -i --clahe

../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc --clahe

# RGB reference
../bin/hpsv rgb -m truecolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc

# RGB CLAHE
../bin/hpsv rgb -m truecolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc --clahe

