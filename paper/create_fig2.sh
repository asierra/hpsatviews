#!/bin/bash
set -e

#1.daynite: Huracán Otis (Rápida Intensificación), 20232980100, mexico
#2. Ash RGB: Erupción del Popocatépetl (Alerta Amarilla Fase 3), 20240582056, ejevolcanico
#3. c02 con clahe, 20242201320, ejevolcanico
#4. Pseudocolor L2 (CTP): "Norte" severo en el Golfo de México, 20240481800, a5
#5. Air Mass RGB: Corriente en Chorro (Jet Stream) Invernál, 20223560000, a2
#Fecha: 23 de diciembre de 2022 (Tormenta invernal Elliott)
#6. Infrarrojo Severo o Fire Temperature (Convección o Incendios), 20261632200, a4
#Fechas sugeridas: Entre el 10 y el 12 de junio de 2026 (durante su pico de actividad en el Pacífico).
#Hora óptima: Entre las 21:30 y las 23:00 UTC.


# 3. Process the 6 illustrative cases
# 1. True Color (Huracán Otis - Rápida Intensificación)

echo "[2/5] Generating (a) True Color (Hurricane / centromex)..."
ANCHORF=/opt/data/2023298/OR_ABI-L1b-RadF-M6C01_G16_s20232980100207_e20232980109515_c20232980109545.nc
hpsv rgb -G \
    --out a_otis.tif \
    "$ANCHORF"
mapdrawer a_otis.tif --clip mexico -o a_otis.jpg --outsize 512

echo "[3/5] Generating (b) Ash RGB (Volcano / ash)..."
ANCHORF=/opt/data/l2-2024058/OR_ABI-L2-CMIPC-M6C01_G16_s20240582056174_e20240582058547_c20240582059013.nc
hpsv rgb \
    --mode ash -G \
    --out b_ash.tif \
    "$ANCHORF"
mapdrawer b_ash.tif --clip ejevolcanico -o b_ash.jpg --outsize 512

echo "[4/5] Generating (c) C02 CLAHE ..."
ANCHORF=/opt/data/2024220/OR_ABI-L1b-RadF-M6C02_G16_s20242201320205_e20242201329513_c20242201329544.nc
hpsv gray --clahe -G \
    --out c_clahe.tif \
    "$ANCHORF"
mapdrawer c_clahe.tif --clip a3 -o c_clahe.jpg --outsize 512

echo "[5/5] Generating (d) Pseudocolor L2 (CTP / SEMAR A5)..."
ANCHORF=/opt/data/l2-2024048/OR_ABI-L2-CTPF-M6_G16_s20240481800208_e20240481809516_c20240481813147.nc
hpsv pseudocolor -G \
        -p cld_top_press.cpt \
        --out d_ctp.tif \
		"$ANCHORF"
mapdrawer d_ctp.tif --clip a5 -o d_ctp.jpg --outsize 512

echo "[6/5] Generating (e) Air Mass RGB (Jet Stream / SEMAR A2)..."
ANCHORF=/opt/data/2022356/OR_ABI-L1b-RadF-M6C08_G16_s20223560000208_e20223560009516_c20223560009575.nc
hpsv rgb \
    --mode airmass -G \
    --out e_airmass.tif \
    "$ANCHORF"
mapdrawer e_airmass.tif --clip a2 -o e_airmass.jpg --outsize 512

echo "[7/5] Generating (f) Severe Storms RGB (Tropical Storm / SEMAR A4)..."
ANCHORF=/opt/data/2026163/OR_ABI-L1b-RadF-M6C01_G19_s20261632200220_e20261632209528_c20261632209582.nc
hpsv rgb \
    --mode severestorm -G \
    --out f_severestorm.tif \
    "$ANCHORF"
mapdrawer f_severestorm.tif --clip a4 -o f_severestorm.jpg --outsize 512


# --- Montage Assembly ---
echo ""
echo "Assembly: Creating the final 2x3 composite panel..."
if command -v montage &> /dev/null; then
    montage \
      -label "(a)" a_otis.jpg \
      -label "(b)" b_ash.jpg \
      -label "(c)" c_clahe.jpg \
      -label "(d)" d_ctp.jpg \
      -label "(e)" e_airmass.jpg \
      -label "(f)" f_severestorm.jpg \
      -tile 2x3 \
      -geometry +15+15 \
      -font "Courier-Bold" \
      -pointsize 24 \
      -background white \
      -fill black \
      $OUT_DIR/hpsv_panel.png
    echo "Success: hpsv_panel.png created"
else
    echo "Warning: 'montage' (ImageMagick) not found. Skipping panel assembly."
fi

echo ""
echo "Done."
