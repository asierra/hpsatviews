#!/bin/bash
# Verifica que el lector rápido de chunks (libdeflate, src/reader_nc_chunk.c)
# produce salida byte-idéntica a la ruta de fallback (nc_get_var), forzada con
# HPSV_DISABLE_FAST_READ=1. Como los píxeles coinciden y el encoder PNG es
# determinista, los archivos resultantes deben ser idénticos (cmp).
set -e

C13=../sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc
C01=../sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc

# Un solo canal (gray)
../bin/hpsv gray "$C13" -i -o fastread_fast.png
HPSV_DISABLE_FAST_READ=1 ../bin/hpsv gray "$C13" -i -o fastread_slow.png
cmp fastread_fast.png fastread_slow.png

# Tres canales (truecolor) para ejercitar C01/C02/C03 y sus distintos tamaños
../bin/hpsv rgb "$C01" --mode truecolor -o fastread_tc_fast.png
HPSV_DISABLE_FAST_READ=1 ../bin/hpsv rgb "$C01" --mode truecolor -o fastread_tc_slow.png
cmp fastread_tc_fast.png fastread_tc_slow.png

echo "OK: lector rápido (libdeflate) byte-idéntico al fallback nc_get_var."
