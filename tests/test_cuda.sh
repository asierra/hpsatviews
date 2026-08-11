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

# True color + Rayleigh + ratio sharpening + stretch: la configuración que hay
# que usar para comparar contra geo2grid, porque es el producto que geo2grid
# emite. Hasta que existió ratio_sharpen_kernel, --sharpen sacaba a truecolor
# del gate de CUDA y esta corrida caía entera a CPU en silencio (salvo por un
# LOG_WARN), de modo que el "benchmark GPU" medía la ruta OpenMP. Si alguien
# vuelve a excluir una opción del gate, este caso no falla — sigue pasando,
# porque ambas rutas dan lo mismo — así que la vigilancia real es el grep de
# abajo sobre el aviso.
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor --rayleigh --sharpen --stretch \
    -o sharpen_cpu.png
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor --rayleigh --sharpen --stretch \
    --cuda -o sharpen_cuda.png 2> sharpen_cuda.log
./compare_image.sh sharpen_cuda.png sharpen_cpu.png
if grep -q "isn't GPU-accelerated" sharpen_cuda.log; then
    echo "  FALLO: --sharpen sacó a truecolor de la ruta CUDA (cayó a CPU)"
    exit 1
else
    echo "  OK: --sharpen se mantiene en la ruta CUDA"
fi

# Reproyección geos -> lat/lon (-G), vecino más cercano (gray, bpp=1): ejercita
# reproject_kernel en la rama nearest.
../bin/hpsv gray -v "$ANCHOR_C13" -i -G -o reproj_gray_cpu.png
../bin/hpsv gray -v "$ANCHOR_C13" -i -G --cuda -o reproj_gray_cuda.png
./compare_image.sh reproj_gray_cuda.png reproj_gray_cpu.png

# Reproyección (-G) bilineal (truecolor, bpp=3): ejercita la rama bilineal del
# reproject_kernel y el relleno de nodata fuera del disco.
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor -G -o reproj_tc_cpu.png
../bin/hpsv rgb -v "$ANCHOR_C01" --mode truecolor -G --cuda -o reproj_tc_cuda.png
./compare_image.sh reproj_tc_cuda.png reproj_tc_cpu.png

# Handoff residente: con --cuda la composición deja la imagen RGB en device y la
# reproyección la consume sin volver a subirla. HPSV_NO_DEVICE_HANDOFF=1 fuerza
# el H2D. Ambos caminos deben dar bytes IDÉNTICOS (no basta la tolerancia de
# compare_image.sh: es literalmente la misma memoria, cualquier diferencia
# significa que el espejo en device quedó obsoleto — típicamente un paso nuevo
# que modifica final_image en host sin marcar ctx->final_image_touched).
../bin/hpsv rgb -v "$ANCHOR_C01" -m truecolor --rayleigh -G --cuda -o handoff_on.png
HPSV_NO_DEVICE_HANDOFF=1 ../bin/hpsv rgb -v "$ANCHOR_C01" -m truecolor --rayleigh -G \
    --cuda -o handoff_off.png
if cmp -s handoff_on.png handoff_off.png; then
    echo "  OK: handoff residente byte-idéntico al camino con H2D"
else
    echo "  FALLO: el handoff residente difiere del camino con H2D"
    exit 1
fi

# daynite entero en device: pseudocolor nocturno, máscara y mezcla en GPU. Es la
# cadena más larga del pipeline, así que acumula más redondeo que truecolor; la
# tolerancia de compare_image.sh (fuzz 2%) la absorbe, y el porcentaje nocturno
# debe coincidir exactamente porque la máscara es una decisión con umbral.
../bin/hpsv rgb -v "$ANCHOR_C01" --mode daynite -G -o daynite_cpu.png
../bin/hpsv rgb -v "$ANCHOR_C01" --mode daynite -G --cuda -o daynite_cuda.png
./compare_image.sh daynite_cuda.png daynite_cpu.png

echo "OK: salida CUDA equivalente a la ruta CPU en los 11 casos."
