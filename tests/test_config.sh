#!/bin/bash
# Test básico para validar el módulo config.c
# Sprint 1: Fundamentos

echo "=== Test del módulo config.c (Sprint 1) ==="
echo

# Colors para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Contador de tests
PASSED=0
FAILED=0

# Archivos de prueba
C13_FILE="sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201801171_e20242201803556_c20242201804056.nc"
C02_FILE="sample_data/OR_ABI-L2-CMIPC-M6C02_G16_s20242201801171_e20242201803544_c20242201804036.nc"

# Función auxiliar para tests
test_command() {
    local desc="$1"
    local cmd="$2"
    
    echo -n "Test: $desc ... "
    
    # Ejecutar comando y capturar salida
    output=$(eval "$cmd" 2>&1)
    exit_code=$?
    
    # Verificar que no haya errores de parseo o segfaults
    if echo "$output" | grep -qiE "(error|segmentation|abort|failed)"; then
        echo -e "${RED}✗ FAIL${NC}"
        echo "  Comando: $cmd"
        echo "  Output: $output"
        ((FAILED++))
        return 1
    else
        echo -e "${GREEN}✓ PASS${NC}"
        ((PASSED++))
        return 0
    fi
}

# Verificar que el binario existe
if [ ! -x "./bin/hpsv" ]; then
    echo -e "${RED}Error: ./bin/hpsv no existe o no es ejecutable${NC}"
    echo "Ejecuta 'make' primero."
    exit 1
fi

# Verificar que los archivos de prueba existen
if [ ! -f "$C13_FILE" ]; then
    echo -e "${RED}Error: archivo de prueba no encontrado: $C13_FILE${NC}"
    exit 1
fi

echo "Archivos de prueba disponibles:"
echo "  - $C13_FILE"
echo "  - $C02_FILE"
echo

echo "--- Tests Básicos ---"
echo

test_command "Programa ejecuta con --version" \
    "./bin/hpsv --version"

test_command "Help funciona" \
    "./bin/hpsv --help"

echo
echo "--- Tests de Parseo de Opciones (config_from_argparser) ---"
echo

# Estos tests validan que config_from_argparser() parsea correctamente
# las opciones sin causar errores

test_command "Opción --gamma" \
    "./bin/hpsv gray $C13_FILE --gamma 1.5 --help"

test_command "Opción --clahe" \
    "./bin/hpsv gray $C13_FILE --clahe --help"

test_command "Opción --clahe-params" \
    "./bin/hpsv gray $C13_FILE --clahe-params 8,8,4.0 --help"

test_command "Opción --histo" \
    "./bin/hpsv gray $C13_FILE --histo --help"

test_command "Opción --invert" \
    "./bin/hpsv gray $C13_FILE --invert --help"

test_command "Opción --scale" \
    "./bin/hpsv gray $C13_FILE --scale 2 --help"

test_command "Opción --clip con coordenadas" \
    "./bin/hpsv gray $C13_FILE --clip '-100,25,-90,15' --help"

test_command "Opción --geographics" \
    "./bin/hpsv gray $C13_FILE --geographics --help"

test_command "Opción --geotiff" \
    "./bin/hpsv gray $C13_FILE --geotiff --help"

test_command "Opción --alpha" \
    "./bin/hpsv gray $C13_FILE --alpha --help"

test_command "Múltiples opciones combinadas" \
    "./bin/hpsv gray $C13_FILE --gamma 1.5 --clahe --scale 2 --help"

echo
echo "--- Tests RGB ---"
echo

test_command "RGB con --mode" \
    "./bin/hpsv rgb $C02_FILE --mode truecolor --help"

test_command "RGB con --rayleigh" \
    "./bin/hpsv rgb $C02_FILE --rayleigh --help"

test_command "RGB con --citylights" \
    "./bin/hpsv rgb $C02_FILE --citylights --help"

echo
echo "--- Tests de Álgebra de Bandas ---"
echo

test_command "Opción --expr" \
    "./bin/hpsv gray $C13_FILE --expr 'C13' --help"

test_command "Opción --expr con --minmax" \
    "./bin/hpsv gray $C13_FILE --expr 'C13' --minmax '0,255' --help"

echo
echo "--- Resumen ---"
echo -e "Tests pasados: ${GREEN}${PASSED}${NC}"
echo -e "Tests fallidos: ${RED}${FAILED}${NC}"
echo

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ Todos los tests pasaron${NC}"
    exit 0
else
    echo -e "${RED}✗ Algunos tests fallaron${NC}"
    exit 1
fi
