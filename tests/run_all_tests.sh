#!/bin/bash
# Script maestro de tests para el proyecto hpsatviews
# Ejecuta todos los tests de validación
#
# Uso: puede invocarse desde la raíz del repo o desde cualquier directorio.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================"
echo "  Test Suite - hpsatviews"
echo "========================================"
echo

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

TOTAL_PASSED=0
TOTAL_FAILED=0

# run_test_suite <nombre> <script relativo a SCRIPT_DIR> [<workdir>]
#   workdir: directorio desde el que se ejecuta el script (por defecto: REPO_DIR)
#   Los scripts con ../bin/hpsv deben ejecutarse desde SCRIPT_DIR (tests/).
#   Los scripts con ./bin/hpsv deben ejecutarse desde REPO_DIR.
run_test_suite() {
    local suite_name="$1"
    local test_script="$SCRIPT_DIR/$2"
    local work_dir="${3:-$REPO_DIR}"

    echo -e "${YELLOW}=== $suite_name ===${NC}"

    if [ ! -f "$test_script" ]; then
        echo -e "${RED}✗ Script no encontrado: $test_script${NC}"
        ((TOTAL_FAILED++))
        echo
        return 0
    fi

    chmod +x "$test_script"

    if (cd "$work_dir" && bash "$test_script") > /tmp/hpsv_test_output.txt 2>&1; then
        passed=$(grep -oP '(?<=Tests pasados:\s)\d+' /tmp/hpsv_test_output.txt | head -1)
        failed=$(grep -oP '(?<=Tests fallidos:\s)\d+' /tmp/hpsv_test_output.txt | head -1)

        # Scripts sin contadores propios: contar la suite como 1 prueba
        if [ -z "$passed" ] && [ -z "$failed" ]; then
            passed=1; failed=0
        fi
        [ -z "$passed" ] && passed=0
        [ -z "$failed" ] && failed=0

        TOTAL_PASSED=$((TOTAL_PASSED + passed))
        TOTAL_FAILED=$((TOTAL_FAILED + failed))
        echo -e "${GREEN}✓ Suite completada${NC}: $passed pasados, $failed fallidos"
    else
        echo -e "${RED}✗ Suite falló${NC}"
        cat /tmp/hpsv_test_output.txt
        ((TOTAL_FAILED++))
    fi

    echo
}

# Compilar proyecto primero
echo -e "${YELLOW}Compilando proyecto...${NC}"
if make -C "$REPO_DIR" clean > /dev/null 2>&1 && make -C "$REPO_DIR" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Compilación exitosa${NC}"
    echo
else
    echo -e "${RED}✗ Compilación falló${NC}"
    exit 1
fi

# test_config.sh usa ./bin/hpsv y sample_data/ → ejecutar desde REPO_DIR
run_test_suite "Config Parser"  "test_config.sh"  "$REPO_DIR"

# Los demás usan ../bin/hpsv y ../sample_data/ → ejecutar desde tests/ (SCRIPT_DIR)
run_test_suite "Pseudocolor"    "test_pseudo.sh"  "$SCRIPT_DIR"
run_test_suite "RGB Composites" "test_rgb.sh"     "$SCRIPT_DIR"
run_test_suite "CLAHE"          "test_clahe.sh"   "$SCRIPT_DIR"

# Resumen final
echo "========================================"
echo "  Resumen Global"
echo "========================================"
echo -e "Total tests pasados: ${GREEN}${TOTAL_PASSED}${NC}"
echo -e "Total tests fallidos: ${RED}${TOTAL_FAILED}${NC}"
echo

if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓✓✓ TODOS LOS TESTS PASARON ✓✓✓${NC}"
    exit 0
else
    echo -e "${RED}✗✗✗ ALGUNOS TESTS FALLARON ✗✗✗${NC}"
    exit 1
fi
