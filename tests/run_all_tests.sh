#!/bin/bash
# Script maestro de tests para el proyecto hpsatviews
# Ejecuta todos los tests de validación

set -e

echo "========================================"
echo "  Test Suite - hpsatviews"
echo "========================================"
echo

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

TOTAL_PASSED=0
TOTAL_FAILED=0

run_test_suite() {
    local suite_name="$1"
    local test_script="$2"
    
    echo -e "${YELLOW}=== $suite_name ===${NC}"
    
    if [ ! -x "$test_script" ]; then
        echo -e "${RED}✗ Test script no encontrado o no ejecutable: $test_script${NC}"
        ((TOTAL_FAILED++))
        return 1
    fi
    
    if $test_script > /tmp/test_output.txt 2>&1; then
        # Extraer resumen del output
        passed=$(grep "Tests pasados:" /tmp/test_output.txt | grep -oP '\d+' | head -1)
        failed=$(grep "Tests fallidos:" /tmp/test_output.txt | grep -oP '\d+' | head -1)
        
        if [ -z "$passed" ]; then passed=0; fi
        if [ -z "$failed" ]; then failed=0; fi
        
        TOTAL_PASSED=$((TOTAL_PASSED + passed))
        TOTAL_FAILED=$((TOTAL_FAILED + failed))
        
        echo -e "${GREEN}✓ Suite completada${NC}: $passed pasados, $failed fallidos"
    else
        echo -e "${RED}✗ Suite falló${NC}"
        cat /tmp/test_output.txt
        ((TOTAL_FAILED++))
        return 1
    fi
    
    echo
}

# Compilar proyecto primero
echo -e "${YELLOW}Compilando proyecto...${NC}"
if make clean > /dev/null 2>&1 && make > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Compilación exitosa${NC}"
    echo
else
    echo -e "${RED}✗ Compilación falló${NC}"
    exit 1
fi

# Ejecutar suites de tests
run_test_suite "SPRINT 1: Config Parser" "./tests/test_config.sh"
run_test_suite "SPRINT 2: Metadata & JSON" "./tests/test_metadata_json"
run_test_suite "SPRINT 4: Feature Flags & Dispatchers" "./tests/test_sprint4_processing.sh"
run_test_suite "SPRINT 5: Pipeline v2 Completo" "./tests/test_sprint5_complete.sh"

# Resumen final
echo "========================================"
echo "  Resumen Global"
echo "========================================"
echo -e "Total tests pasados: ${GREEN}${TOTAL_PASSED}${NC}"
echo -e "Total tests fallidos: ${TOTAL_FAILED}${RED}${NC}"
echo

if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓✓✓ TODOS LOS TESTS PASARON ✓✓✓${NC}"
    exit 0
else
    echo -e "${RED}✗✗✗ ALGUNOS TESTS FALLARON ✗✗✗${NC}"
    exit 1
fi
