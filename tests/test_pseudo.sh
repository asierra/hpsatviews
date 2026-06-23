#!/bin/bash
set -e

# Clean previous data
#rm *.png

# Gray channel to compare
../bin/hpsv gray -v ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc --expr "C13-C15"

# Internal pseudocolor (sin -p: usa la paleta interna por defecto, rainbow)
../bin/hpsv pseudocolor -v -s -4 ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o pseudo_out.png
./compare_image.sh pseudo_out.png expected_output/ref_pseudo.png

# Internal pseudocolor inverted
../bin/hpsv pseudocolor -v -i ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc

# Using palette file (paleta discreta de fase de nube)
../bin/hpsv pseudocolor -v -s -4 -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -o pseudo_phase_out.png
./compare_image.sh pseudo_phase_out.png expected_output/ref_pseudo_phase.png

# NOTE: CTPC/ACTPC product tests require external data files (not in sample_data/)
# ../bin/hpsv pseudocolor -v ../sample_data/OR_ABI-L2-CTPC-M6_G16_...nc
# ../bin/hpsv pseudocolor -v -p ../assets/phase.cpt ../sample_data/OR_ABI-L2-ACTPC-M6_G16_...nc


