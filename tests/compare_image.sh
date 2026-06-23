#!/bin/bash
# Compara una imagen generada contra una referencia con tolerancia, usando ImageMagick.
#
# Uso: compare_image.sh <generada.png|tif> <referencia.png|tif> [tolerancia_pct]
#   tolerancia_pct: porcentaje máximo de píxeles que pueden diferir (con un fuzz de
#   color del 2%, para absorber redondeos entre builds/CPUs) antes de considerarse
#   una regresión. Por defecto 1%.
#
# Para TIFF/GeoTIFF se fuerza la página [0] (resolución base), ya que los COG
# generados incluyen overviews embebidos como páginas adicionales que confunden
# a compare/identify si no se selecciona explícitamente.
#
# Requiere: ImageMagick (compare, identify).
set -euo pipefail

GENERATED="$1"
REFERENCE="$2"
TOLERANCE_PCT="${3:-1.0}"
FUZZ="2%"

if [ ! -f "$GENERATED" ]; then
    echo "FAIL: no existe la imagen generada: $GENERATED" >&2
    exit 1
fi
if [ ! -f "$REFERENCE" ]; then
    echo "FAIL: no existe la imagen de referencia: $REFERENCE" >&2
    exit 1
fi

GENERATED_PAGE="$GENERATED"
REFERENCE_PAGE="$REFERENCE"
if [[ "$GENERATED" =~ \.(tif|tiff)$ ]] || [[ "$REFERENCE" =~ \.(tif|tiff)$ ]]; then
    GENERATED_PAGE="${GENERATED}[0]"
    REFERENCE_PAGE="${REFERENCE}[0]"
fi

TOTAL_PX=$(identify -format "%[fx:w*h]" "$REFERENCE_PAGE" 2>/dev/null | head -1)
AE_RAW=$(compare -metric AE -fuzz "$FUZZ" "$GENERATED_PAGE" "$REFERENCE_PAGE" null: 2>&1 || true)
AE=$(printf '%s\n' "$AE_RAW" | head -1 | grep -oE '^[0-9]+' || true)

if [ -z "$AE" ]; then
    echo "FAIL: $GENERATED vs $REFERENCE -> $AE_RAW" >&2
    exit 1
fi

MAX_AE=$(awk -v t="$TOTAL_PX" -v p="$TOLERANCE_PCT" 'BEGIN { printf "%d", t * p / 100 }')

if [ "$AE" -le "$MAX_AE" ]; then
    echo "OK: $GENERATED vs $REFERENCE (píxeles distintos: $AE, tolerancia: $MAX_AE)"
    exit 0
else
    echo "FAIL: $GENERATED vs $REFERENCE (píxeles distintos: $AE > tolerancia: $MAX_AE)" >&2
    exit 1
fi
