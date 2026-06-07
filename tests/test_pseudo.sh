#!/bin/bash
set -e

# Clean previous data
#rm *.png

# Gray channel to compare
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc --expr "C13-C15"

# Internal pseudocolor
../bin/hpsv pseudocolor -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

# Internal pseudocolor inverted
../bin/hpsv pseudocolor -v -i ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

# Using palette file
../bin/hpsv pseudocolor -v -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

# NOTE: CTPC/ACTPC product tests require external data files (not in sample_data/)
# ../bin/hpsv pseudocolor -v ../sample_data/OR_ABI-L2-CTPC-M6_G16_...nc
# ../bin/hpsv pseudocolor -v -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-ACTPC-M6_G16_...nc


