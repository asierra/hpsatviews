#!/bin/bash

# Comparar resultados de hpsatviews y gdal

# Recorte

# Dataset
DATASET="/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20253231800210_e20253231809518_c20253231809563.nc"

# Eliminar archivos que inhiben a gdal
rm -f a2_sp_gdal.tif a2_rp_gdal.tif a2_sp_gdal.png a2_rp_gdal.png

# ---------------------------------------------------------
# CASO 1: Recorte sin reproyección (Mantiene proyección GOES)
# ---------------------------------------------------------
./hpsatviews singlegray \
    --clip -126.7976178202817 38.2939198810346 -78.4256323272443 9.6021231973635 \
    -o a2_sp_hpsv.png \
    "$DATASET"
    
mapdrawer --bounds -126.7976178202817 38.2939198810346 -78.4256323272443 9.6021231973635 \
    --layer COASTLINE:cyan:0.5  --crs goes16 a2_sp_hpsv.png

# 1. Crear GeoTIFF recortado (Proyección nativa GOES preservada)
gdalwarp \
    -te -126.7976178202817 9.6021231973635 -78.4256323272443 38.2939198810346 \
    -te_srs EPSG:4326 \
    -crop_to_cutline \
    "NETCDF:\"$DATASET\":Rad" \
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
    --clip -126.7976178202817 38.2939198810346 -78.4256323272443 9.6021231973635 \
    -o a2_rp_hpsv.png \
    "$DATASET"
    
mapdrawer --bounds -126.7976178202817 38.2939198810346 -78.4256323272443 9.6021231973635 \
    --layer COASTLINE:cyan:0.5  a2_rp_hpsv.png
    
# 1. Crear GeoTIFF reproyectado a EPSG:4326
gdalwarp \
    -t_srs EPSG:4326 \
    -te -126.7976178202817 9.6021231973635 -78.4256323272443 38.2939198810346 \
    "NETCDF:\"$DATASET\":Rad" \
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
