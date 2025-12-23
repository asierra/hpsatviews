#!/bin/bash
set -e

DATADIR=/data/input/abi/l1b/fd
OUTDIR=/data/output/abi/vistas
OUTDIR=./

# Get the latest L1b C01 file from DATADIR
NC_C01_FILE=$DATADIR/OR_ABI-L1b-RadF-M6C01_G19_s20253551600217_e20253551609526_c20253551609562.nc

# Run default composite
echo "Generating True Color composite"
../bin/hpsatviews_debug rgb -m truecolor -v --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}.png \
    "$NC_C01_FILE"

#echo "Proyecting True Color composite"
#../bin/hpsatviews rgb -m truecolor -v --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}_geo.png \
#    "$NC_C01_FILE" -r
#    
#echo "✅ Composite test finished."
