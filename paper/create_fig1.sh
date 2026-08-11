#!/bin/bash

# This full disk data is not in the sample-data, you have to get it by yourself
PATH_TO_ANCHOR_FILE=/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20261721800229_e20261721809537_c20261721809579.nc
hpsv rgb -B -o fd.tif "$PATH_TO_ANCHOR_FILE"

mapdrawer fd_geo.tif --clip atlantic -o atlantic.jpg --outsize 512

mapdrawer fd_geo.tif --clip a5 -o a5.jpg --outsize 512x320

magick atlantic.jpg \( a5.jpg -background black -splice 0x2+0+0 \) -append cuts.jpg

mapdrawer fd.tif --outsize 512 -o fd.jpg

mapdrawer fd_geo.tif --outsize 512 -o fd_geo.jpg

magick fd.jpg fd_geo.jpg cuts.jpg -background black -splice 10x0+0+0 +append reprojection.jpg

rm a5.jpg  atlantic.jpg  cuts.jpg  fd_geo.jpg  fd_geo.tif  fd.jpg  fd.tif

