#!/bin/bash
# Test script para validar la corrección de clipping con esquinas fuera del disco
# Fecha: 1 de diciembre de 2025

set -e  # Exit on error

echo "=========================================="
echo "Test: Corrección de Clipping con Esquinas Fuera del Disco"
echo "=========================================="
echo ""

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Verificar que existe un archivo NetCDF de prueba
# Ajusta esta ruta según tu entorno
NC_FILE=""
if [ -f "/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20253231800210_e20253231809518_c20253231809563.nc" ]; then
    NC_FILE="/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20253231800210_e20253231809518_c20253231809563.nc"
elif [ -f "$(ls -t /data/input/abi/l1b/fd/*.nc 2>/dev/null | head -1)" ]; then
    NC_FILE="$(ls -t /data/input/abi/l1b/fd/*.nc 2>/dev/null | head -1)"
else
    echo -e "${RED}ERROR: No se encontró archivo NetCDF de prueba${NC}"
    echo "Por favor, especifica la ruta a un archivo GOES L1b en la variable NC_FILE"
    exit 1
fi

echo "Usando archivo NetCDF: $NC_FILE"
echo ""

# Crear directorio de salida para pruebas
TEST_OUT_DIR="./test_clip_corners"
mkdir -p "$TEST_OUT_DIR"
echo "Directorio de salida: $TEST_OUT_DIR"
echo ""

# Test 1: Región normal (todas las esquinas dentro del disco) - NO DEBE CAMBIAR
echo -e "${YELLOW}Test 1: Región Normal - México Central${NC}"
echo "  Dominio: lon[-107.23, -93.84], lat[14.94, 22.72]"
echo "  Expectativa: Sin inferencia, comportamiento idéntico al anterior"
./hpsatviews rgb -m truecolor \
    --clip -107.23 22.72 -93.84 14.94 \
    --verbose \
    -o "$TEST_OUT_DIR/test1_normal_mexico.png" \
    "$NC_FILE" 2>&1 | grep -E "(Inferencia|esquinas|infer|UL|UR|LL|LR)" || echo "  ✓ Sin inferencias (esperado)"
echo ""

# Test 2: Región amplia con Upper Left fuera del disco
echo -e "${YELLOW}Test 2: Región Amplia - UL Fuera del Disco${NC}"
echo "  Dominio: lon[-135.0, -60.0], lat[10.0, 50.0]"
echo "  Expectativa: UL inferida desde LL y UR"
./hpsatviews rgb -m truecolor \
    --clip -135.0 50.0 -60.0 10.0 \
    --verbose \
    -o "$TEST_OUT_DIR/test2_wide_ul_out.png" \
    "$NC_FILE" 2>&1 | grep -E "(Inferencia|inferida|esquinas)" || echo "  ⚠ No se detectó inferencia (verificar manualmente)"
echo ""

# Test 3: Región amplia con reproyección (PRE-clip)
echo -e "${YELLOW}Test 3: Región Amplia + Reproyección${NC}"
echo "  Dominio: lon[-135.0, -60.0], lat[10.0, 50.0]"
echo "  Expectativa: Inferencia en PRE-reproyección"
./hpsatviews rgb -m truecolor \
    --clip -135.0 50.0 -60.0 10.0 \
    -r \
    --verbose \
    -o "$TEST_OUT_DIR/test3_wide_reproj.png" \
    "$NC_FILE" 2>&1 | grep -E "(PRE-reproyección|Inferencia|inferida)" || echo "  ⚠ No se detectó inferencia PRE"
echo ""

# Test 4: Región muy amplia (múltiples esquinas fuera)
echo -e "${YELLOW}Test 4: Región Muy Amplia - Múltiples Esquinas Fuera${NC}"
echo "  Dominio: lon[-140.0, -50.0], lat[5.0, 55.0]"
echo "  Expectativa: Inferencia de 2 o más esquinas"
./hpsatviews rgb -m truecolor \
    --clip -140.0 55.0 -50.0 5.0 \
    --verbose \
    -o "$TEST_OUT_DIR/test4_very_wide.png" \
    "$NC_FILE" 2>&1 | grep -E "(Inferidas [2-4]|esquinas)" || echo "  ⚠ No se detectaron inferencias múltiples"
echo ""

# Test 5: Región completamente fuera del disco (debe fallar gracefully)
echo -e "${YELLOW}Test 5: Región Fuera del Disco - Europa${NC}"
echo "  Dominio: lon[0.0, 20.0], lat[40.0, 60.0]"
echo "  Expectativa: Error claro, procesamiento continúa sin recortar"
./hpsatviews rgb -m truecolor \
    --clip 0.0 60.0 20.0 40.0 \
    --verbose \
    -o "$TEST_OUT_DIR/test5_outside.png" \
    "$NC_FILE" 2>&1 | grep -E "(fuera del disco|solo [0-1] esquinas)" || echo "  ⚠ No se detectó error esperado"
echo ""

# Test 6: Caso del compara_gdal.sh (región amplia conocida)
echo -e "${YELLOW}Test 6: Caso compara_gdal.sh - Región Amplia CONUS${NC}"
echo "  Dominio: lon[-126.80, -78.43], lat[9.60, 38.29]"
echo "  Expectativa: Inferencia de al menos 1 esquina"
./hpsatviews rgb -m truecolor \
    --clip -126.7976178202817 38.2939198810346 -78.4256323272443 9.6021231973635 \
    --verbose \
    -o "$TEST_OUT_DIR/test6_conus_wide.png" \
    "$NC_FILE" 2>&1 | grep -E "(Inferencia|inferida)" || echo "  ℹ Sin inferencia (puede ser normal si todas las esquinas están dentro)"
echo ""

echo "=========================================="
echo -e "${GREEN}Tests Completados${NC}"
echo "=========================================="
echo ""
echo "Imágenes de salida generadas en: $TEST_OUT_DIR"
echo ""
echo "Verificación Manual Recomendada:"
echo "  1. Comparar test1 con versión anterior (no debe haber diferencias)"
echo "  2. Verificar test2 y test3 incluyen toda el área válida sin 'mochar'"
echo "  3. Verificar test5 generó imagen (sin recorte) o abortó limpiamente"
echo ""
echo "Para ver los logs completos, ejecuta los comandos con --verbose"
echo ""
