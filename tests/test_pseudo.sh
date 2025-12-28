#!/bin/bash
set -e

# Clean previous data
rm *.png

# Gray channel to compare
../bin/hpsv gray -v -i ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc

# Internal pseudo
../bin/hpsv pseudo -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc

# Internal pseudo inverted
../bin/hpsv pseudo -v -i ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc

# Using palette file with discrete data
../bin/hpsv pseudo -v -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-ACTPC-M6_G16_s20240581931174_e20240581933547_c20240581935039.nc 

# Using internal palette with discrete data
../bin/hpsv pseudo -v ../sample_data/OR_ABI-L2-ACTPC-M6_G16_s20240581931174_e20240581933547_c20240581935039.nc 


