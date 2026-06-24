#!/bin/bash
# Regenerates the example PNGs embedded in paper.md.
#
# They are intentionally not committed to git (paper/*.png matches the
# repo's blanket *.png .gitignore rule): gray/pseudocolor/truecolor are
# copies of the regression-test reference images already tracked under
# tests/expected_output/, and ash/daynite are generated fresh from
# sample_data/ since those modes have no committed pixel-reference (see
# CLAUDE.md: they only get a non-blank sanity check in the test suite).
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PROJECT_ROOT="$(dirname "$DIR")"
cd "$PROJECT_ROOT"

if [ ! -x bin/hpsv ]; then
    echo "Error: bin/hpsv not found. Build it first: make"
    exit 1
fi

ANCHOR_C13=$(ls sample_data/OR_ABI-L2-CMIPC-M6C13_G16_*.nc 2>/dev/null | head -1)
ANCHOR_C01=$(ls sample_data/OR_ABI-L2-CMIPC-M6C01_G16_*.nc 2>/dev/null | head -1)

if [ -z "$ANCHOR_C13" ] || [ -z "$ANCHOR_C01" ]; then
    echo "Error: sample_data/ is missing the C13/C01 channels needed for the ash/daynite figures."
    echo "Run: bash reproduction/download_sample.sh"
    exit 1
fi

echo "[1/5] Copying gray example from tests/expected_output/..."
cp tests/expected_output/ref_gray.png paper/gray_example.png

echo "[2/5] Copying pseudocolor example from tests/expected_output/..."
cp tests/expected_output/ref_pseudo.png paper/pseudocolor_example.png

echo "[3/5] Copying true color example from tests/expected_output/..."
cp tests/expected_output/ref_truecolor.png paper/truecolor_example.png

echo "[4/5] Generating ash example (rgb -m ash)..."
bin/hpsv rgb -m ash -s -4 "$ANCHOR_C13" -o paper/ash_example.png

echo "[5/5] Generating day/night example (rgb -m daynite)..."
bin/hpsv rgb -m daynite -s -4 "$ANCHOR_C01" -o paper/daynite_example.png

echo
echo "Done. Figures in paper/:"
ls -lh paper/*.png
