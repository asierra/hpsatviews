#!/bin/bash

# Comparar resultados de hpsatviews y gdal

# Dataset
#DATASET="/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20253361800210_e20253361809518_c20253361809549.nc"
#DATASET="/data/ceniza/2019/spring/OR_ABI-L2-CMIPC-M3C01_G16_s20190871342161_e20190871344534_c20190871345000.nc"
DATASET="/data/output/abi/l2/fd/CG_ABI-L2-CMIPF-M6C01_G19_s20253101600214_e20253101609522_c20253101620053.nc"

# Variable del NetCDF a usar (depende del tipo de producto)
#NCVAR="Rad"    # Para productos L1b (Radiance)
NCVAR="CMI"   # Para productos L2 (Cloud and Moisture Imagery)

# Coordenadas de recorte: lon_min lat_max lon_max lat_min
#CLIP_COORDS="-126.7976178202817 38.2939198810346 -78.4256323272443 9.6021231973635"
# Ejemplos de otras regiones:
# México centro: "-107.23 22.72 -93.84 14.94"
# Golfo de México: "-98.0 30.0 -80.0 18.0"
CLIP_COORDS="-107.23 22.72 -93.84 14.94"

# Comprueba que existe DATASET
if [ ! -f "$DATASET" ]; then
    echo "ERROR: Dataset no encontrado: $DATASET"
    exit 1
fi

# Eliminar archivos que inhiben a gdal
rm -f a2_sp_gdal.tif a2_rp_gdal.tif a2_sp_gdal.png a2_rp_gdal.png

# Convertir coordenadas para gdalwarp (te requiere: xmin ymin xmax ymax)
read LON_MIN LAT_MAX LON_MAX LAT_MIN <<< "$CLIP_COORDS"
GDAL_TE="$LON_MIN $LAT_MIN $LON_MAX $LAT_MAX"

# ---------------------------------------------------------
# CASO 1: Recorte sin reproyección (Mantiene proyección GOES)
# ---------------------------------------------------------
./hpsatviews singlegray \
    --clip $CLIP_COORDS \
    -o a2_sp_hpsv.png \
    "$DATASET" -v
    
mapdrawer --bounds $CLIP_COORDS \
    --layer COASTLINE:cyan:0.5  --crs goes16 a2_sp_hpsv.png

./hpsatviews singlegray \
    --clip $CLIP_COORDS \
    -o a2_sp_hpsv.tif \
    "$DATASET" -v

# 1. Crear GeoTIFF recortado (Proyección nativa GOES preservada)
gdalwarp \
    -te $GDAL_TE \
    -te_srs EPSG:4326 \
    -crop_to_cutline \
    "NETCDF:\"$DATASET\":$NCVAR" \
    a2_sp_gdal.tif

# 2. DIBUJAR MAPA (Usando shapefile reproyectado a GOES)
# Usamos burn 2000 para que destaque en blanco brillante
gdal_rasterize \
    -b 1 \
    -burn 2000 \
    -l referencia_gdal \
    /usr/local/share/lanot/shapefiles/referencia_gdal.shp \
    a2_sp_gdal.tif

# 3. Convertir a PNG para visualización
gdal_translate \
    -of PNG \
    -scale 0 5 0 255 \
    a2_sp_gdal.tif \
    a2_sp_gdal.png
    
# ---------------------------------------------------------
# CASO 2: Recorte con reproyección (Transformado a Lat/Lon)
# ---------------------------------------------------------
./hpsatviews singlegray \
    --geographics \
    --clip $CLIP_COORDS \
    -o a2_rp_hpsv.png \
    "$DATASET"
    
mapdrawer --bounds $CLIP_COORDS \
    --layer COASTLINE:cyan:0.5  a2_rp_hpsv.png

./hpsatviews singlegray \
    --geographics \
    --clip $CLIP_COORDS \
    -o a2_rp_hpsv.tif \
    "$DATASET"
    
# 1. Crear GeoTIFF reproyectado a EPSG:4326
gdalwarp \
    -t_srs EPSG:4326 \
    -te $GDAL_TE \
    "NETCDF:\"$DATASET\":$NCVAR" \
    a2_rp_gdal.tif

# 2. DIBUJAR MAPA (Usando shapefile estándar WGS84)
gdal_rasterize \
    -b 1 \
    -burn 2000 \
    -l ne_10m_coastline \
    /usr/local/share/lanot/shapefiles/ne_10m_coastline.shp \
    a2_rp_gdal.tif

# 3. Convertir a PNG para visualización
gdal_translate \
    -of PNG \
    -scale 0 5 0 255 \
    a2_rp_gdal.tif \
    a2_rp_gdal.png

gdalinfo a2_sp_gdal.tif > gdlinfo.a2_sp_gdal.tif.out
gdalinfo a2_sp_hpsv.tif > gdlinfo.a2_sp_hpsv.tif.out
gdalinfo a2_rp_hpsv.tif > gdlinfo.a2_rp_hpsv.tif.out
gdalinfo a2_rp_gdal.tif > gdlinfo.a2_rp_gdal.tif.out