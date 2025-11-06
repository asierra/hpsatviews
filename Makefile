CC=gcc
CFLAGS=-g -I. -Wall -std=c99
CFLAGSP=-fopenmp 
DEPS = datanc.h  image.h  reader_nc.h  writer_png.h  logger.h
LIBOBJS = truecolor_rgb.o reader_nc.o writer_png.o singlegray.o \
	nocturnal_pseudocolor.o daynight_mask.o image.o datanc.o logger.o
OBJ = main.o 

LIBS=-lnetcdf -lpng -lm

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

truecolornight: $(OBJ) libhpsatviews.a
	$(CC) -o $@ $^ $(CFLAGSP) $(LIBS)

singlegray: singlegraymain.o libhpsatviews.a args.o
	$(CC) -o $@ $^ $(CFLAGSP) $(LIBS)

geos2geographics: geos2geographics.o libhpsatviews.a args.o
	$(CC) -o $@ $^ $(CFLAGSP) $(LIBS)

.PHONY: clean

libhpsatviews.a: $(LIBOBJS)
	rm -f libhpsatviews.a
	ar scr libhpsatviews.a $(LIBOBJS)

clean:
	rm -f *.o *~ truecolornight