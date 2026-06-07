#!/bin/bash
# Download GOES-16 CONUS test data from the NOAA public S3 bucket.
# Strategy: list the bucket index for a specific hour and download the first
# available scan for each required channel.
#
# Scene: Day 220 of 2024 (August 7, 2024) at 13:00 UTC
# At this time the solar terminator crosses the US Pacific coast (~120°W),
# placing ~85% of CONUS in full daylight — ideal for Rayleigh correction
# showcase — while the west coast is at dawn, enabling the day/night blend.

# Resolve project root regardless of where this script is called from
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
if [[ "$(basename "$SCRIPT_DIR")" == "reproduction" ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi

DATA_DIR="${PROJECT_ROOT}/sample_data"
mkdir -p "$DATA_DIR"

BUCKET_URL="https://noaa-goes16.s3.amazonaws.com"
PRODUCT_PREFIX="ABI-L2-CMIPC/2024/220/13" # CONUS, Year 2024, Day 220, Hour 13 UTC

echo "--- Downloading sample data (GOES-16 CONUS, Day 220/2024, 13:00 UTC) ---"
echo "Destination: $DATA_DIR"

# Query the S3 bucket index for the target hour (returns XML listing)
echo "Querying NOAA S3 index..."
FILE_LIST=$(curl -s "${BUCKET_URL}?list-type=2&prefix=${PRODUCT_PREFIX}")

# Download the first available scan for a given ABI channel number (zero-padded)
download_channel() {
    local CH=$1
    local LABEL=$2
    printf "  C%s (%s)... " "$CH" "$LABEL"

    local FILE_KEY
    FILE_KEY=$(echo "$FILE_LIST" | grep -o "<Key>[^<]*M6C${CH}_[^<]*</Key>" | head -n 1 | sed 's/<\/\?Key>//g')

    if [ -z "$FILE_KEY" ]; then
        echo "NOT FOUND in S3 index"
        return 1
    fi

    local LOCAL_NAME
    LOCAL_NAME=$(basename "$FILE_KEY")

    if [ -f "$DATA_DIR/$LOCAL_NAME" ]; then
        echo "already present, skipping"
        return 0
    fi

    curl -f -s -S -o "$DATA_DIR/$LOCAL_NAME" "${BUCKET_URL}/${FILE_KEY}" \
        && echo "done" \
        || echo "DOWNLOAD FAILED"
}

# Channels required for each demo product:
#   truecolor / daynite : C01 (Blue), C02 (Red), C03 (Veggie)
#   daynite night side  : C13 (Clean IR)
#   ash composite       : C11, C13, C14, C15
download_channel "01" "Blue"
download_channel "02" "Red"
download_channel "03" "Veggie/NIR"
download_channel "11" "Cloud-top phase IR"
download_channel "13" "Clean IR longwave (reference)"
download_channel "14" "IR longwave"
download_channel "15" "Dirty IR longwave"

echo ""
echo "--- Download complete ---"
ls -lh "$DATA_DIR"/*.nc 2>/dev/null || echo "(no .nc files found)"
