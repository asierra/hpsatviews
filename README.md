# High Performance Satellite Views

High performance satellite image processing and views generation for the GOES family.

It creates both a true color image for the day and a gray scale plus a meteorological colormap for the night and high clouds, a composite with both, and saves corresponding PNG images ready for fast, operational visualization. As it is C compiled code, it does it in a fraction of a second, compared to minutes with other Python based tools. In a future it will use parallelization and will be even faster.

This version doesn't need L2 data, it uses L1b data directly.

External dependency: libnetcdf and libpng.

License: GPL 3.0 Copyright (c) 2025 Alejandro Aguilar Sierra (asierra@unam.mx)

