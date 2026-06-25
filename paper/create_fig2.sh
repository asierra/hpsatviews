#!/bin/bash
set -e

1.daynite: Huracán Otis (Rápida Intensificación), 20232972130, 
2. Ash RGB: Erupción del Popocatépetl (Alerta Amarilla Fase 3), 20240582056,
3. Day/Night Blend (El terminador sobre el territorio nacional)
mejlor gris con realce
4. Pseudocolor L2 (CTP): "Norte" severo en el Golfo de México, 20240481800,
5. Air Mass RGB: Corriente en Chorro (Jet Stream) Invernál
Fecha: 23 de diciembre de 2022 (Tormenta invernal Elliott) o mediados de enero de 2024.

Hora sugerida: 12:00 UTC (madrugada).

6. Infrarrojo Severo o Fire Temperature (Convección o Incendios)
Fechas sugeridas: Entre el 10 y el 12 de junio de 2026 (durante su pico de actividad en el Pacífico).

Hora óptima: Entre las 21:30 y las 23:00 UTC.


# Función para localizar un archivo ancla exacto de GOES (NetCDF)
# Uso: find_goes_scene <TIMESTAMP> <DIRECTORIO_BASE> <PATRON>
# Ejemplo: find_goes_scene "20232972130" "/depot/goes-east/l1b/abi/fd" "C01"
find_goes_scene() {
    local TIMESTAMP=$1
    local BASE_DIR=$2
    local PATTERN=${3:-""} # Patrón arbitrario (ej. "C01", "CTP", "C13")

    if [ -z "$TIMESTAMP" ] || [ "${#TIMESTAMP}" -ne 11 ]; then
        echo "Error: El timestamp '$TIMESTAMP' es inválido. Debe ser YYYYDDDHHMM." >&2
        return 1
    fi

    # Extraer año y día juliano para armar el árbol de directorios
    local YYYY=${TIMESTAMP:0:4}
    local DDD=${TIMESTAMP:4:3}
    local TARGET_DIR="${BASE_DIR}/${YYYY}/${DDD}"

    if [ ! -d "$TARGET_DIR" ]; then
        echo "Error: El directorio no existe -> $TARGET_DIR" >&2
        return 1
    fi

    # Buscar el primer archivo que contenga el patrón y el inicio de escaneo (_s)
    local SEARCH_STR="*${PATTERN}*_s${TIMESTAMP}*.nc"
    local MATCH
    MATCH=$(find "$TARGET_DIR" -maxdepth 1 -name "$SEARCH_STR" -print -quit)

    if [ -z "$MATCH" ]; then
        echo "Error: No se encontró archivo ancla con el patrón '$PATTERN' para s${TIMESTAMP} en $TARGET_DIR" >&2
        return 1
    fi

    # Devolver la ruta absoluta lista para hpsv
    echo "$MATCH"
}


# 2. Setup Data Depot and Outputs
DATA_DEPOT="/depot/goes-east/l1b/abi/fd/2026/173"
# Directorios base en tu clúster
L1B_DEPOT="/depot/goes-east/l1b/abi/fd"
L2_CTP_DEPOT="/depot/goes-east/l2/ctp/conus" # Ajusta según tu árbol real de L2

# 3. Process the 6 illustrative cases
# 1. True Color (Huracán Otis - Rápida Intensificación)
echo "[2/5] Generating (a) True Color (Hurricane / centromex)..."
ANCHORF=$(find_goes_scene "20232972130" "$L1B_DEPOT")
bin/hpsv rgb \
    --mode truecolor \
    --rayleigh \
    --extent $EXT_CENTROMEX \
    --out $OUT_DIR/panel_a_truecolor.png \
    "$REF_L1B"

echo "[3/5] Generating (b) Ash RGB (Volcano / ash)..."
bin/hpsv rgb \
    --mode ash \
    --extent $EXT_ASH \
    --out $OUT_DIR/panel_b_ash.png \
    "$REF_L1B"

echo "[4/5] Generating (c) Day/Night Blend (Terminator / mexico)..."
bin/hpsv rgb \
    --mode daynite \
    --extent $EXT_MEXICO \
    --out $OUT_DIR/panel_c_daynite.png \
    "$REF_L1B"

echo "[5/5] Generating (d) Pseudocolor L2 (CTP / SEMAR A5)..."
if [ -n "$REF_CTP" ]; then
    bin/hpsv pseudocolor \
        --palette rainbow \
        --extent $EXT_A5 \
        --out $OUT_DIR/panel_d_ctp.png \
        "$REF_CTP"
else
    echo "  -> Skipping CTP: Anchor file not found."
fi

echo "[6/5] Generating (e) Air Mass RGB (Jet Stream / SEMAR A2)..."
bin/hpsv rgb \
    --mode airmass \
    --extent $EXT_A2 \
    --out $OUT_DIR/panel_e_airmass.png \
    "$REF_L1B"

echo "[7/5] Generating (f) Severe Storms RGB (Tropical Storm / SEMAR A4)..."
bin/hpsv rgb \
    --mode severestorm \
    --extent $EXT_A4 \
    --out $OUT_DIR/panel_f_severestorm.png \
    "$REF_L1B"

# --- Montage Assembly ---
echo ""
echo "Assembly: Creating the final 2x3 composite panel..."
if command -v montage &> /dev/null; then
    montage \
      -label "(a)" $OUT_DIR/panel_a_truecolor.png \
      -label "(b)" $OUT_DIR/panel_b_ash.png \
      -label "(c)" $OUT_DIR/panel_c_daynite.png \
      -label "(d)" $OUT_DIR/panel_d_ctp.png \
      -label "(e)" $OUT_DIR/panel_e_airmass.png \
      -label "(f)" $OUT_DIR/panel_f_severestorm.png \
      -tile 2x3 \
      -geometry +15+15 \
      -font "Courier-Bold" \
      -pointsize 24 \
      -background white \
      -fill black \
      $OUT_DIR/hpsv_panel.png
    echo "Success: hpsv_panel.png created in $OUT_DIR/"
else
    echo "Warning: 'montage' (ImageMagick) not found. Skipping panel assembly."
fi

echo ""
echo "Done. Results in $OUT_DIR/"
