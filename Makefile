CC=gcc
CFLAGS=-g -I./include -Wall -std=c11 -D_POSIX_C_SOURCE=200809L -fopenmp $(shell gdal-config --cflags)
LDFLAGS=-lm -lnetcdf -lpng -fopenmp $(shell gdal-config --libs)

# Nombre del ejecutable final
TARGET = hpsatviews

# Directorio de archivos fuente
SRC_DIR = src
# Directorio de archivos objeto (se crean en raíz)
OBJ_DIR = .

# Archivos de cabecera en include/
DEPS = $(wildcard include/*.h)

# Lista de archivos fuente
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/rgb.c \
       $(SRC_DIR)/processing.c \
       $(SRC_DIR)/reprojection.c \
       $(SRC_DIR)/args.c \
       $(SRC_DIR)/datanc.c \
       $(SRC_DIR)/daynight_mask.c \
       $(SRC_DIR)/image.c \
       $(SRC_DIR)/logger.c \
       $(SRC_DIR)/nocturnal_pseudocolor.c \
       $(SRC_DIR)/rayleigh.c \
       $(SRC_DIR)/rayleigh_lut_embedded.c \
       $(SRC_DIR)/reader_cpt.c \
       $(SRC_DIR)/reader_nc.c \
       $(SRC_DIR)/reader_png.c \
       $(SRC_DIR)/singlegray.c \
       $(SRC_DIR)/truecolor_rgb.c \
       $(SRC_DIR)/writer_geotiff.c \
       $(SRC_DIR)/writer_png.c \
       $(SRC_DIR)/filename_utils.c \
       $(SRC_DIR)/clip_loader.c

# Archivos objeto (en raíz)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ_DIR)/*.o *~ $(TARGET)