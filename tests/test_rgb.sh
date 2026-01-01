#!/bin/bash
set -e

# Reference
../bin/hpsv rgb -m truecolor -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc -o "truecolor_reference.png"

# Rayleigh LUTs
../bin/hpsv rgb -m truecolor --rayleigh -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc -o "truecolor_ray_luts.png"

# Rayleigh Analytic
../bin/hpsv rgb -m truecolor --ray-analytic -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc -o "truecolor_ray_analytic.png"
