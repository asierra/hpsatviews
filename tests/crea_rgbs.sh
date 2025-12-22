#!/bin/bash
set -e

DATADIR=/data/input/abi/l1b/fd
OUTDIR=/data/output/abi/vistas

# Get the latest L1b C01 file from DATADIR
NC_C01_FILE=$DATADIR/OR_ABI-L1b-RadF-M6C01_G19_s20253231800210_e20253231809518_c20253231809563.nc

# Run default composite
echo "Generating True Color composite"
../bin/hpsatviews rgb --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}.tif \
    "$NC_C01_FILE"

echo "Proyecting True Color composite"
../bin/hpsatviews rgb --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}_geo.tif \
    "$NC_C01_FILE" -r
    
echo "✅ Composite test finished."
