#!/bin/bash
# Test SPRINT 4: Validar integración de run_processing_v2()
# Verifica que los comandos gray y pseudocolor funcionen con el feature flag

PASSED=0
FAILED=0

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "=== SPRINT 4: Test de Processing Commands ==="

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

# Verificar que hpsv existe
echo "Verificando binario..."
[ -f bin/hpsv ]
test_check "Binario bin/hpsv existe"

# Verificar modo legacy (debe funcionar sin warnings de stub)
echo -e "\n--- Modo Legacy (PIPELINE_V2=0) ---"
echo "Recompilando en modo legacy..."
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1 || { echo "Error compilando modo legacy"; exit 1; }

# Test gray con archivo real
./bin/hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --gamma 1.5 -o /tmp/test_gray_legacy.png > /dev/null 2>&1
test_check "gray legacy: genera imagen"
[ -f /tmp/test_gray_legacy.png ]
test_check "gray legacy: archivo existe"

# Test pseudocolor con archivo real
./bin/hpsv pseudocolor sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --cpt assets/phase.cpt -o /tmp/test_pseudo_legacy.png > /dev/null 2>&1
test_check "pseudocolor legacy: genera imagen"
[ -f /tmp/test_pseudo_legacy.png ]
test_check "pseudocolor legacy: archivo existe"

# Verificar modo v2 (debe mostrar warning de stub)
echo -e "\n--- Modo V2 (PIPELINE_V2=1) ---"
echo "Recompilando en modo v2..."
make clean > /dev/null 2>&1
make -j4 PIPELINE_V2=1 > /dev/null 2>&1 || { echo "Error compilando modo v2"; exit 1; }

# Test gray v2 (debe funcionar correctamente ahora)
./bin/hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --gamma 1.5 -o /tmp/test_gray_v2.png > /dev/null 2>&1
test_check "gray v2: genera imagen exitosamente"
[ -f /tmp/test_gray_v2.png ]
test_check "gray v2: archivo existe"
[ -f /tmp/test_gray_v2.json ]
test_check "gray v2: genera JSON sidecar"

# Test pseudocolor v2 (debe funcionar correctamente ahora)
./bin/hpsv pseudocolor sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc \
    --cpt assets/phase.cpt -o /tmp/test_pseudo_v2.png > /dev/null 2>&1
test_check "pseudocolor v2: genera imagen exitosamente"
[ -f /tmp/test_pseudo_v2.png ]
test_check "pseudocolor v2: archivo existe"
[ -f /tmp/test_pseudo_v2.json ]
test_check "pseudocolor v2: genera JSON sidecar"

# Verificar feature flag en código
echo -e "\n--- Verificación de Código ---"
grep -q "HPSV_USE_NEW_PIPELINE" include/processing.h
test_check "processing.h: feature flag presente"

grep -q "run_processing_v2" include/processing.h
test_check "processing.h: declaración run_processing_v2()"

grep -q "run_processing_v2.*ProcessConfig.*MetadataContext" src/processing.c
test_check "processing.c: implementación run_processing_v2()"

grep -q "HPSV_USE_NEW_PIPELINE" src/main.c
test_check "main.c: usa feature flag en dispatchers"

# Test de compilación condicional
grep -A 10 "cmd_gray" src/main.c | grep -q "#if HPSV_USE_NEW_PIPELINE"
test_check "cmd_gray: tiene branch condicional"

grep -A 10 "cmd_pseudocolor" src/main.c | grep -q "#if HPSV_USE_NEW_PIPELINE"
test_check "cmd_pseudocolor: tiene branch condicional"

# Cleanup
echo -e "\n--- Limpieza ---"
rm -f /tmp/test_gray_*.png /tmp/test_pseudo_*.png /tmp/test_*.json
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1  # Restaurar binario legacy

# Resumen
echo -e "\n=== Resumen ==="
echo -e "${GREEN}Pasados: $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Fallidos: $FAILED${NC}"
    exit 1
else
    echo "✅ SPRINT 4: Todos los tests pasaron correctamente"
fi
