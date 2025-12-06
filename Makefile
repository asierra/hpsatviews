CC=gcc
CFLAGS=-g -I. -Wall -std=c11 -D_POSIX_C_SOURCE=200809L -fopenmp $(shell gdal-config --cflags)
LDFLAGS=-lm -lnetcdf -lpng -fopenmp $(shell gdal-config --libs)

# Nombre del ejecutable final
TARGET = hpsatviews

# Archivos de cabecera. La regla de compilación depende de ellos.
DEPS = args.h datanc.h image.h logger.h processing.h reader_cpt.h \
       reader_nc.h reprojection.h rayleigh.h rgb.h writer_png.h \
       filename_utils.h rayleigh_lut_embedded.h \
       writer_geotiff.h clip_loader.h

# Archivos objeto a compilar.
# Se incluyen los nuevos módulos y se eliminan los 'main' antiguos.
OBJS = main.o \
       rgb.o \
       processing.o \
       reprojection.o \
       args.o \
       datanc.o \
       daynight_mask.o \
       image.o \
       logger.o \
       nocturnal_pseudocolor.o \
       rayleigh.o \
       rayleigh_lut_embedded.o \
       reader_cpt.o \
       reader_nc.o \
       singlegray.o \
       truecolor_rgb.o \
       writer_geotiff.o \
       writer_png.o \
       filename_utils.o \
       clip_loader.o

.PHONY: all clean

all: $(TARGET)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o *~ $(TARGET)