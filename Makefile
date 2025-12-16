CC=gcc
CFLAGS=-g -I./include -Wall -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -fopenmp $(shell gdal-config --cflags)
LDFLAGS=-lm -lnetcdf -lpng -fopenmp $(shell gdal-config --libs)

# Nombre del ejecutable final
TARGET = hpsatviews

# Directorios
SRC_DIR = src
# Cambio clave: definimos una carpeta dedicada para objetos
OBJ_DIR = obj

# Archivos de cabecera en include/
DEPS = $(wildcard include/*.h)

# Lista de archivos fuente
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/rgb.c \
       $(SRC_DIR)/processing.c \
       $(SRC_DIR)/reprojection.c \
       $(SRC_DIR)/args.c \
       $(SRC_DIR)/channelset.c \
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
       $(SRC_DIR)/truecolor.c \
       $(SRC_DIR)/writer_geotiff.c \
       $(SRC_DIR)/writer_png.c \
       $(SRC_DIR)/filename_utils.c \
       $(SRC_DIR)/clip_loader.c

# Generación de la lista de objetos: 
# Cambia src/archivo.c -> obj/archivo.o
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

# Regla para crear el directorio de objetos si no existe
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Regla de compilación
# Nota el "| $(OBJ_DIR)". Esto es un "order-only prerequisite".
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS) | $(OBJ_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

# Linkeo final
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) *~

