#!/bin/bash
set -e

# Clean previous data
#rm *.png

# Gray channel to compare
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc --expr "C13-C15"

# Internal pseudo
../bin/hpsv pseudo -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc

# Internal pseudo inverted
../bin/hpsv pseudo -v -i ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc

# Presure
../bin/hpsv pseudo -v ../sample_data/OR_ABI-L2-CTPC-M6_G16_s20240581741174_e20240581743547_c20240581746306.nc

# Using palette file with discrete data
../bin/hpsv pseudo -v -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-ACTPC-M6_G16_s20240581741174_e20240581743547_c20240581745192.nc

# Using internal palette with discrete data
../bin/hpsv pseudo -v ../sample_data/OR_ABI-L2-ACTPC-M6_G16_s20240581741174_e20240581743547_c20240581745192.nc


