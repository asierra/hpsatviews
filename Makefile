# ==========================================
#  HPSATVIEWS - High Performance Makefile
# ==========================================

# --- Configuración del Compilador ---
CC = gcc

# --- Idioma (en por defecto | es opcional) ---
HPSV_LANG ?= en

# Banderas base: C11 estándar, advertencias, OpenMP
# Nota: -march=native optimiza para la CPU donde se compila (ideal para HPC local)
CFLAGS_COMMON = -Wall -Wextra -std=c11 -fopenmp -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $(shell gdal-config --cflags)
LDFLAGS = -lm -lnetcdf -lpng -fopenmp $(shell gdal-config --libs)

# --- Flags de idioma ---
CFLAGS_LANG =

ifeq ($(HPSV_LANG),es)
    CFLAGS_LANG += -DHPSV_LANG_ES
endif

# --- Modos de Compilación ---
# Uso: make (Release por defecto) | make debug (para desarrollo con gdb)
ifdef DEBUG
    CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_LANG) -g -O0 -DDEBUG_MODE
    TARGET_NAME = hpsatviews_debug
else
    # Release: Optimización máxima (-O3) y nativa de la arquitectura
    CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_LANG) -O3 -march=native -funroll-loops
    TARGET_NAME = hpsatviews
endif

# --- Directorios ---
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
# Prefijo de instalación estándar (Linux)
PREFIX = /usr/local

# --- Archivos ---
# Busca automáticamente todos los .c en src/ (Cero mantenimiento)
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Genera la lista de objetos esperados en obj/
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
# El ejecutable final con ruta
TARGET = $(BIN_DIR)/$(TARGET_NAME)

# Inclusión de cabeceras
CFLAGS += -I$(INC_DIR)

# ==========================================
#  Reglas de Construcción
# ==========================================

.PHONY: all clean install uninstall directories debug info

all: directories $(TARGET)
	@echo "========================================"
	@echo " Build Complete: $(TARGET)"
	@echo " Mode: $(if $(DEBUG),Debug,Release (HPC Optimized))"
	@echo "========================================"

# Regla para enlazar el ejecutable
$(TARGET): $(OBJS)
	@echo "Linking $@"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Regla genérica para compilar objetos
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Crear directorios si no existen
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

# Limpieza
clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJ_DIR) $(BIN_DIR)

# Instalación (para el usuario final)
install: all
	@echo "Installing into $(PREFIX)/bin..."
	@install -d $(PREFIX)/bin
	@install -m 755 $(TARGET) $(PREFIX)/bin/hpsatviews
	@echo "Installation successful."

uninstall:
	@rm -f $(PREFIX)/bin/hpsatviews
	@echo "Uninstalled hpsatviews."

# Ayuda para debuggear el Makefile
info:
	@echo "Source files found: $(SRCS)"
	@echo "Object files target: $(OBJS)"
	@echo "Build mode: $(if $(DEBUG),debug,release)"
	@echo "Language: $(HPSV_LANG)"
	@echo "CFLAGS: $(CFLAGS)"
