#!/bin/bash
set -e

DATADIR=/data/input/abi/l1b/fd
OUTDIR=/data/output/abi/vistas
OUTDIR=./

# Get the latest L1b C01 file from DATADIR
NC_C01_FILE=$(ls -t "$DATADIR"/*C01*.nc 2>/dev/null | head -n 1)

# Run default composite
echo "Generating True Color/Night composite"
../bin/hpsv rgb -v --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}.png \
    "$NC_C01_FILE" 

echo "Proyecting True Color/Night composite"
../bin/hpsv rgb -v --out $OUTDIR/{YYYY}.{MM}.{DD}.{hh}.{mm}.{SAT}.{CH}_geo.png \
    "$NC_C01_FILE" -r

