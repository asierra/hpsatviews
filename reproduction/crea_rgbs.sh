#!/bin/bash
set -e

DATADIR=/data/input/abi/l1b/fd
OUTDIR=/data1/output/abi/vistas

# Get the latest L1b C01 file from DATADIR
NC_C01_FILE=

# Run default composite
echo "Generating True Color composite"
hpsatviews rgb --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}.tif \
    "$NC_C01_FILE"

echo "Proyecting True Color composite"
hpsatviews rgb --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}.tif \s
    "$NC_C01_FILE" -r
    
echo "✅ Composite test finished."
