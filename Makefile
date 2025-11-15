CC=gcc
CFLAGS=-g -I. -Wall -std=c11 -fopenmp
LDFLAGS=-lm -lnetcdf -lpng -fopenmp

# Nombre del ejecutable final
TARGET = hpsatviews

# Archivos de cabecera. La regla de compilación depende de ellos.
DEPS = args.h datanc.h image.h logger.h processing.h reader_cpt.h \
       reader_nc.h reprojection.h rgb.h writer_png.h

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
       reader_cpt.o \
       reader_nc.o \
       singlegray.o \
       truecolor_rgb.o \
       writer_png.o

.PHONY: all clean

all: $(TARGET)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o *~ $(TARGET)