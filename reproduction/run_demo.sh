#!/bin/bash
set -e

# Determine the project root directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# If the script is in reproduction/, the root is one level up.
# If the script is in root, DIR is root.
if [[ "$(basename "$DIR")" == "reproduction" ]]; then
    PROJECT_ROOT="$(dirname "$DIR")"
else
    PROJECT_ROOT="$DIR"
fi

cd "$PROJECT_ROOT"

# 1. Clean compilation
echo "[1/3] Compiling project..."
make clean > /dev/null
make

# Define a reference file from sample_data
# Using C01 as reference, the program should find other channels in the same directory
REF_FILE="sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201801171_e20242201803544_c20242201804035.nc"

if [ ! -f "$REF_FILE" ]; then
    echo "❌ Error: Sample data file not found: $REF_FILE"
    echo "Please ensure sample data is in the 'sample_data' directory."
    exit 1
fi

# Ensure output directory exists
mkdir -p reproduction

# 2. Run True Color
echo "[2/3] Generating True Color..."
bin/hpsatviews rgb \
    --mode truecolor \
    --out reproduction/demo_truecolor.tif \
    --verbose \
    "$REF_FILE"

# 3. Run Ash (Ceniza)
echo "[3/3] Generating Ash Product..."
bin/hpsatviews rgb \
    --mode ash \
    --out reproduction/demo_ash.tif \
    --verbose \
    "$REF_FILE"

echo "✅ Demo finished. Check results in 'reproduction/'"

# 4. Run Composite (Complex Test)
echo "[4/4] Generating default composite (Complex Test)..."
bin/hpsatviews rgb \
    --geographics \
    --clip mexico \
    --out reproduction/test_composite.tif \
    --verbose \
    "$REF_FILE"

echo "✅ Composite test finished."
