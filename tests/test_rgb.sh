#!/bin/bash
set -e

DATADIR=/data/input/abi/l1b/fd
OUTDIR=/data/output/abi/vistas
OUTDIR=./

# Get the latest L1b C01 file from DATADIR
NC_C01_FILE=$DATADIR/OR_ABI-L1b-RadF-M6C01_G19_s20253101600214_e20253101609522_c20253101609553.nc

# Run default composite
echo "Generating True Color composite"
../bin/hpsv rgb -m truecolor -v "$NC_C01_FILE"

#echo "Proyecting True Color composite"
#../bin/hpsv rgb -m truecolor -v --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}_geo.png \
#    "$NC_C01_FILE" -r
#    
#echo "✅ Composite test finished."
