#!/bin/bash
set -e

files=(
    OR_ABI-L1b-RadF-M6C01_G19_s20260281200213_e20260281209522_c20260281209563.nc
    OR_ABI-L1b-RadF-M6C01_G19_s20260281400213_e20260281409522_c20260281409570.nc
    OR_ABI-L1b-RadF-M6C01_G19_s20260281600214_e20260281609522_c20260281609569.nc
    OR_ABI-L1b-RadF-M6C01_G19_s20260281800211_e20260281809520_c20260281809561.nc
    OR_ABI-L1b-RadF-M6C01_G19_s20260282000211_e20260282009520_c20260282009551.nc
)

path="../sample_data/028"

for file in "${files[@]}"; do
	temp="${file#*_s}"  # Elimina todo hasta _s
	TS="${temp:0:11}"   # Toma los primeros 11 caracteres
	echo "$TS"
    ../bin/hpsv rgb $path/$file -r -o "master_{TS}_lalo.png" > master_"$TS"_lalo.log 2>&1
done
