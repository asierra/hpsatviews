#!/bin/bash
# Test SPRINT 5: Validar implementación completa del pipeline v2
# Verifica que gray, pseudocolor y rgb funcionan correctamente con outputs idénticos

PASSED=0
FAILED=0

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "=== SPRINT 5: Test de Pipeline Completo v2 ==="

test_check() {
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓${NC} $1"
        ((PASSED++))
    else
        echo -e "${RED}✗${NC} $1"
        ((FAILED++))
        return 1
    fi
}

# --- TEST 1: GRAY ---
echo -e "\n--- Test 1: Gray Mode ---"
echo "Compilando modo legacy..."
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1 || { echo "Error compilando legacy"; exit 1; }

./bin/hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --gamma 1.5 -o /tmp/gray_legacy.png > /dev/null 2>&1
test_check "gray legacy: genera imagen"

echo "Compilando modo v2..."
make clean > /dev/null 2>&1
make -j4 PIPELINE_V2=1 > /dev/null 2>&1 || { echo "Error compilando v2"; exit 1; }

./bin/hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --gamma 1.5 -o /tmp/gray_v2.png > /dev/null 2>&1
test_check "gray v2: genera imagen"

[ -f /tmp/gray_v2.json ]
test_check "gray v2: genera JSON sidecar"

# Comparar checksums
HASH_LEGACY=$(md5sum /tmp/gray_legacy.png | awk '{print $1}')
HASH_V2=$(md5sum /tmp/gray_v2.png | awk '{print $1}')
[ "$HASH_LEGACY" = "$HASH_V2" ]
test_check "gray: outputs idénticos (md5: $HASH_V2)"

# --- TEST 2: PSEUDOCOLOR ---
echo -e "\n--- Test 2: Pseudocolor Mode ---"
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1

./bin/hpsv pseudocolor sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --cpt assets/phase.cpt -o /tmp/pseudo_legacy.png > /dev/null 2>&1
test_check "pseudocolor legacy: genera imagen"

make clean > /dev/null 2>&1
make -j4 PIPELINE_V2=1 > /dev/null 2>&1

./bin/hpsv pseudocolor sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --cpt assets/phase.cpt -o /tmp/pseudo_v2.png > /dev/null 2>&1
test_check "pseudocolor v2: genera imagen"

[ -f /tmp/pseudo_v2.json ]
test_check "pseudocolor v2: genera JSON sidecar"

HASH_LEGACY=$(md5sum /tmp/pseudo_legacy.png | awk '{print $1}')
HASH_V2=$(md5sum /tmp/pseudo_v2.png | awk '{print $1}')
[ "$HASH_LEGACY" = "$HASH_V2" ]
test_check "pseudocolor: outputs idénticos (md5: $HASH_V2)"

# --- TEST 3: RGB TRUECOLOR ---
echo -e "\n--- Test 3: RGB Truecolor Mode ---"
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1

./bin/hpsv rgb sample_data/OR_ABI-L2-CMIPC-M6C*_G16_s20242201801171*.nc \
    --mode truecolor -o /tmp/rgb_legacy.png > /dev/null 2>&1
test_check "rgb legacy: genera imagen"

make clean > /dev/null 2>&1
make -j4 PIPELINE_V2=1 > /dev/null 2>&1

./bin/hpsv rgb sample_data/OR_ABI-L2-CMIPC-M6C*_G16_s20242201801171*.nc \
    --mode truecolor -o /tmp/rgb_v2.png > /dev/null 2>&1
test_check "rgb v2: genera imagen"

[ -f /tmp/rgb_v2.json ]
test_check "rgb v2: genera JSON sidecar"

HASH_LEGACY=$(md5sum /tmp/rgb_legacy.png | awk '{print $1}')
HASH_V2=$(md5sum /tmp/rgb_v2.png | awk '{print $1}')
[ "$HASH_LEGACY" = "$HASH_V2" ]
test_check "rgb: outputs idénticos (md5: $HASH_V2)"

# --- TEST 4: Verificar metadata JSON ---
echo -e "\n--- Test 4: Validación de Metadatos JSON ---"

grep -q '"command": "gray"' /tmp/gray_v2.json
test_check "gray JSON: contiene comando correcto"

grep -q '"gamma": 1.5' /tmp/gray_v2.json
test_check "gray JSON: contiene gamma correcto"

grep -q '"palette": "assets/phase.cpt"' /tmp/pseudo_v2.json
test_check "pseudocolor JSON: contiene paleta"

grep -q '"mode": "truecolor"' /tmp/rgb_v2.json
test_check "rgb JSON: contiene modo correcto"

grep -q '"output_width"' /tmp/rgb_v2.json
test_check "rgb JSON: contiene dimensiones de salida"

# --- Limpieza ---
echo -e "\n--- Limpieza ---"
rm -f /tmp/gray_*.png /tmp/pseudo_*.png /tmp/rgb_*.png /tmp/*_v2.json
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1  # Restaurar binario legacy

# --- Resumen ---
echo -e "\n=== Resumen ==="
echo -e "${GREEN}Pasados: $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Fallidos: $FAILED${NC}"
    exit 1
else
    echo "✅ SPRINT 5: Todos los tests pasaron correctamente"
    echo "   Pipeline v2 genera outputs idénticos al legacy"
fi
