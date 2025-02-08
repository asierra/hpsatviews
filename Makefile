CC=gcc
CFLAGS=-g -I.
CFLAGSP=-fopenmp -g -I.
DEPS = datanc.h  image.h  reader_nc.h  writer_png.h
OBJ = main.o truecolor_rgb.o reader_nc.o writer_png.o nocturnal_pseudocolor.o

LIBS=-lnetcdf -lpng -lm

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

truecolornight: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGSP) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o *~ truecolornight
