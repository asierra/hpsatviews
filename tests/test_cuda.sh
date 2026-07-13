#!/bin/bash
# Test de equivalencia CUDA vs CPU: la salida de --cuda debe coincidir (con la
# tolerancia de compare_image.sh) con la ruta OpenMP de referencia sobre los
# mismos datos de sample_data/.
#
# Se salta con éxito (exit 0) si el binario no fue compilado con CUDA=1 o si
# no hay GPU NVIDIA visible — así puede registrarse en run_all_tests.sh sin
# romper entornos sin GPU. Para ejecutarlo de verdad:
#   make CUDA=1 && cd tests && ./test_cuda.sh
# o bien: CUDA=1 tests/run_all_tests.sh
set -e

ANCHOR_C13=../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc
ANCHOR_C01=../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc

# ¿Binario compilado con CUDA? (sin soporte, --cuda falla en el parseo de config)
if ../bin/hpsv gray "$ANCHOR_C13" --cuda -o cuda_probe.png 2>&1 | grep -q "without CUDA support"; then
    echo "SKIP: binario sin soporte CUDA (compila con 'make CUDA=1')."
    exit 0
fi
rm -f cuda_probe.png

# ¿Hay GPU visible?
if ! command -v nvidia-smi > /dev/null || ! nvidia-smi -L > /dev/null 2>&1; then
    echo "SKIP: no se detecta GPU NVIDIA (nvidia-smi)."
    exit 0
fi

# Gray IR (C13, invertido): CPU vs CUDA
../bin/hpsv gray -v "$ANCHOR_C13" -i -o gray_cpu.png
../bin/hpsv gray -v "$ANCHOR_C13" -i --cuda -o gray_cuda.png
./compare_image.sh gray_cuda.png gray_cpu.png

# Gray con canal alfa (bpp=2)
../bin/hpsv gray -v "$ANCHOR_C01" -a -o gray_alpha_cpu.png
../bin/hpsv gray -v "$ANCHOR_C01" -a --cuda -o gray_alpha_cuda.png
./compare_image.sh gray_alpha_cuda.png gray_alpha_cpu.png

# Pseudocolor (paleta interna rainbow; ejercita last_color/cpt en el kernel)
../bin/hpsv pseudo -v "$ANCHOR_C13" -o pseudo_cpu.png
../bin/hpsv pseudo -v "$ANCHOR_C13" --cuda -o pseudo_cuda.png
./compare_image.sh pseudo_cuda.png pseudo_cpu.png

# Gamma (C13 invertido): cadena residente en device upload -> gamma -> gray.
# Verifica que la gamma en GPU coincide con dataf_apply_gamma() (CPU) y que
# encadenar una op extra sobre el DataFDev no altera el resultado.
../bin/hpsv gray -v "$ANCHOR_C13" -i --gamma 2.0 -o gamma_cpu.png
../bin/hpsv gray -v "$ANCHOR_C13" -i --gamma 2.0 --cuda -o gamma_cuda.png
./compare_image.sh gamma_cuda.png gamma_cpu.png

# True color por defecto: cadena residente multi-canal (sube C01/C02/C03 una
# vez, verde sintético + compose en GPU). Ejercita green_kernel y multiband_kernel.
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor -o tc_cpu.png
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor --cuda -o tc_cuda.png
./compare_image.sh tc_cuda.png tc_cpu.png

# True color + Rayleigh (LUT): cadena residente completa. Ejercita la navegación
# en device (solar/satellite/relaz desde lat/lon), solar zenith, LUT trilineal y
# relajación por nubes. Compara contra la ruta CPU (nav + Rayleigh en OpenMP).
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor --rayleigh -o tcray_cpu.png
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor --rayleigh --cuda -o tcray_cuda.png
./compare_image.sh tcray_cuda.png tcray_cpu.png

echo "OK: salida CUDA equivalente a la ruta CPU en los 6 casos."
