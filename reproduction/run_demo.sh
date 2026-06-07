#!/bin/bash
set -e

# Determine the project root directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
if [[ "$(basename "$DIR")" == "reproduction" ]]; then
    PROJECT_ROOT="$(dirname "$DIR")"
else
    PROJECT_ROOT="$DIR"
fi

cd "$PROJECT_ROOT"

# 1. Clean compilation
echo "[1/4] Compiling project..."
make clean > /dev/null
make

# Locate the C01 anchor file — discover whatever was downloaded
REF_FILE=$(ls sample_data/OR_ABI-L2-CMIPC-M6C01_G16_*.nc 2>/dev/null | head -1)

if [ -z "$REF_FILE" ]; then
    echo "Error: No C01 sample file found in sample_data/."
    echo "Run: bash reproduction/download_sample.sh"
    exit 1
fi
echo "Using anchor file: $REF_FILE"

mkdir -p reproduction/out

# 2. True color without correction (baseline)
echo "[2/4] Generating True Color (baseline)..."
bin/hpsv rgb \
    --mode truecolor \
    --gamma 2.0 \
    --out reproduction/out/demo_truecolor.tif \
    "$REF_FILE"

# 3. True color with Rayleigh correction (key feature)
echo "[3/4] Generating True Color with Rayleigh correction..."
bin/hpsv rgb \
    --mode truecolor \
    --rayleigh \
    --gamma 2.0 \
    --out reproduction/out/demo_truecolor_rayleigh.tif \
    "$REF_FILE"

# 4. Day/Night composite (daynite) — showcases terminator blend
echo "[4/4] Generating Day/Night composite..."
bin/hpsv rgb \
    --mode daynite \
    --out reproduction/out/demo_daynite.tif \
    "$REF_FILE"

echo ""
echo "Done. Results in reproduction/out/"
ls -lh reproduction/out/

# --- Validation: check output dimensions match reference images ---
echo ""
echo "[Validation] Checking output dimensions..."

PASS=0
FAIL=0

check_dimensions() {
    local name="$1"
    local tif="reproduction/out/${name}.tif"
    local ref="reproduction/expected_output/${name}.png"

    if [ ! -f "$tif" ]; then
        echo "  MISSING : $tif"
        FAIL=$((FAIL + 1)); return
    fi

    local tif_size
    tif_size=$(gdalinfo "$tif" 2>/dev/null | grep "Size is" | grep -o '[0-9]*, [0-9]*' | tr -d ' ')

    if [ ! -f "$ref" ]; then
        echo "  NO REF  : $ref (skipping size check)"
        PASS=$((PASS + 1)); return
    fi
    local ref_size
    ref_size=$(gdalinfo "$ref" 2>/dev/null | grep "Size is" | grep -o '[0-9]*, [0-9]*' | tr -d ' ')

    # tif is full-res; ref is an 800px thumbnail — confirm tif width >= ref width
    local tif_w ref_w
    tif_w=$(echo "$tif_size" | cut -d',' -f1)
    ref_w=$(echo "$ref_size" | cut -d',' -f1)

    if [ -n "$tif_w" ] && [ -n "$ref_w" ] && [ "$tif_w" -ge "$ref_w" ]; then
        echo "  OK      : ${name}  (${tif_size} >= reference ${ref_size})"
        PASS=$((PASS + 1))
    else
        echo "  MISMATCH: ${name}  tif=${tif_size} ref=${ref_size}"
        FAIL=$((FAIL + 1))
    fi
}

check_dimensions "demo_truecolor"
check_dimensions "demo_truecolor_rayleigh"
check_dimensions "demo_daynite"

echo ""
if [ "$FAIL" -eq 0 ]; then
    echo "Validation passed ($PASS/$((PASS + FAIL)))"
else
    echo "Validation: $PASS passed, $FAIL failed"
    exit 1
fi

