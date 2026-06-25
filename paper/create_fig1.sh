#!/bin/bash

# This full disk data is not in the sample-data, you have to get it by yourself
hpsv rgb -B -o fd.tif {PATH_TO_ANCHOR_FILE}

mapdrawer fd_geo.tif --clip atlantic -o aa.jpg --outsize 512

mapdrawer fd_geo.tif --clip a5 -o a5.jpg --outsize 512

magick atlantic.jpg \( a5.jpg -background black -splice 0x2+0+0 \) -append cuts.jpg

mapdrawer fd.tif --outsize 512 -o fd.jpg

mapdrawer fd_geo.tif --outsize 512 -o fd_geo.jpg

magick fd.jpg fd_geo.jpg cuts.jpg -background black -splice 10x0+0+0 +append fig1.jpg

rm a5.jpg  atlantic.jpg  cuts.jpg  fd_geo.jpg  fd_geo.tif  fd.jpg  fd.tif

