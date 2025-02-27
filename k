nocturnal_pseudocolor.c: In function ‘create_nocturnal_pseudocolor’:
nocturnal_pseudocolor.c:21:23: error: ‘DataNC’ has no member named ‘width’
   21 |   imout.width = datanc.width;
      |                       ^
nocturnal_pseudocolor.c:22:24: error: ‘DataNC’ has no member named ‘height’
   22 |   imout.height = datanc.height;
      |                        ^
nocturnal_pseudocolor.c:23:41: error: ‘DataNC’ has no member named ‘size’
   23 |   imout.data = malloc(imout.bpp * datanc.size);
      |                                         ^
nocturnal_pseudocolor.c:30:29: error: ‘DataNC’ has no member named ‘height’
   30 |   for (int y = 0; y < datanc.height; y++) {
      |                             ^
nocturnal_pseudocolor.c:31:31: error: ‘DataNC’ has no member named ‘width’
   31 |     for (int x = 0; x < datanc.width; x++) {
      |                               ^
nocturnal_pseudocolor.c:32:25: error: ‘DataNC’ has no member named ‘width’
   32 |       int i = y * datanc.width + x;
      |                         ^
nocturnal_pseudocolor.c:37:17: error: ‘DataNC’ has no member named ‘data_in’
   37 |       if (datanc.data_in[i] >= 0 && datanc.data_in[i] < 4095) {
      |                 ^
nocturnal_pseudocolor.c:37:43: error: ‘DataNC’ has no member named ‘data_in’
   37 |       if (datanc.data_in[i] >= 0 && datanc.data_in[i] < 4095) {
      |                                           ^
nocturnal_pseudocolor.c:38:25: error: ‘DataNC’ has no member named ‘data_in’
   38 |         float f = datanc.data_in[i];
      |                         ^
make: *** [Makefile:11: nocturnal_pseudocolor.o] Error 1
