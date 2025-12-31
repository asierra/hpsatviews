#!/bin/bash
set -e

# Run default composite
echo "Generating True Color composite"
../bin/hpsv rgb -m truecolor --rayleigh -g 2 -s -4 -v ../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc

#echo "Proyecting True Color composite"
#../bin/hpsv rgb -m truecolor -v --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}_geo.png \
#    "$NC_C01_FILE" -r
#    
#echo "✅ Composite test finished."
