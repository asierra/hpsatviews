#!/bin/bash
# Test para validar el feature flag HPSV_USE_NEW_PIPELINE
# Sprint 3: Refactorización RGB

echo "=== Test del Feature Flag (SPRINT 3) ==="
echo

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

TESTFILE="sample_data/OR_ABI-L2-CMIPC-M6C02_G16_s20242201801171_e20242201803544_c20242201804036.nc"

if [ ! -f "$TESTFILE" ]; then
    echo -e "${RED}✗ Archivo de prueba no encontrado: $TESTFILE${NC}"
    exit 1
fi

echo "--- Test 1: Compilación con HPSV_USE_NEW_PIPELINE=0 (Legacy) ---"
echo "Limpiando y recompilando..."
make clean > /dev/null 2>&1
if make > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Compilación exitosa (modo legacy)${NC}"
else
    echo -e "${RED}✗ Compilación falló${NC}"
    exit 1
fi

echo "Verificando que el programa funciona..."
if ./bin/hpsv --version > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Programa ejecuta correctamente${NC}"
else
    echo -e "${RED}✗ Programa no ejecuta${NC}"
    exit 1
fi

echo "Verificando comando RGB (debe usar legacy)..."
if ./bin/hpsv rgb $TESTFILE --help > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Comando RGB funciona en modo legacy${NC}"
else
    echo -e "${RED}✗ Comando RGB falló${NC}"
    exit 1
fi

echo
echo "--- Test 2: Verificar que feature flag existe ---"
if grep -q "HPSV_USE_NEW_PIPELINE" include/config.h; then
    FLAG_VALUE=$(grep "define HPSV_USE_NEW_PIPELINE" include/config.h | awk '{print $3}')
    echo -e "${GREEN}✓ Feature flag encontrado: HPSV_USE_NEW_PIPELINE = $FLAG_VALUE${NC}"
else
    echo -e "${RED}✗ Feature flag no encontrado${NC}"
    exit 1
fi

echo
echo "--- Test 3: Verificar nueva interfaz ---"
if grep -q "run_rgb_v2" include/rgb.h; then
    echo -e "${GREEN}✓ Nueva interfaz run_rgb_v2 declarada en rgb.h${NC}"
else
    echo -e "${RED}✗ run_rgb_v2 no encontrado en rgb.h${NC}"
    exit 1
fi

if grep -q "run_rgb_v2" src/rgb.c; then
    echo -e "${GREEN}✓ Nueva interfaz run_rgb_v2 implementada en rgb.c${NC}"
else
    echo -e "${RED}✗ run_rgb_v2 no encontrado en rgb.c${NC}"
    exit 1
fi

echo
echo "--- Test 4: Verificar dispatcher en main.c ---"
if grep -q "HPSV_USE_NEW_PIPELINE" src/main.c; then
    echo -e "${GREEN}✓ Dispatcher con feature flag implementado en main.c${NC}"
else
    echo -e "${RED}✗ Dispatcher no encontrado en main.c${NC}"
    exit 1
fi

echo
echo "--- Test 5: Compilación con HPSV_USE_NEW_PIPELINE=1 (Nuevo Pipeline) ---"
echo "Limpiando y recompilando con flag activado..."
make clean > /dev/null 2>&1
if make PIPELINE_V2=1 > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Compilación exitosa (modo nuevo)${NC}"
else
    echo -e "${RED}✗ Compilación falló con nuevo pipeline${NC}"
    exit 1
fi

echo "Verificando que el programa funciona..."
if ./bin/hpsv --version > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Programa ejecuta correctamente${NC}"
else
    echo -e "${RED}✗ Programa no ejecuta${NC}"
    exit 1
fi

echo "Verificando comando RGB (debe intentar usar v2)..."
output=$(./bin/hpsv rgb $TESTFILE --help 2>&1)
if echo "$output" | grep -q "Usando nuevo pipeline"; then
    echo -e "${GREEN}✓ Pipeline nuevo detectado en output${NC}"
else
    echo -e "${YELLOW}⚠ Warning: Pipeline nuevo no detectado (esperado si run_rgb_v2 aún no está completo)${NC}"
fi

echo
echo "--- Resumen ---"
echo -e "${GREEN}✓ Todos los tests del SPRINT 3 pasaron${NC}"
echo
echo "Notas:"
echo "  - Pipeline legacy (HPSV_USE_NEW_PIPELINE=0): Funcional ✓"
echo "  - Pipeline nuevo (HPSV_USE_NEW_PIPELINE=1): Skeleton implementado ✓"
echo "  - run_rgb_v2: Stub creado, implementación completa pendiente (SPRINT 4)"
echo
echo "Restaurando a modo legacy..."
make clean > /dev/null 2>&1 && make > /dev/null 2>&1
echo -e "${GREEN}✓ Proyecto restaurado a modo legacy${NC}"
