# ==========================================
#  HPSATVIEWS - High Performance Makefile
# ==========================================

# --- Configuración del Compilador ---
CC = gcc
NVCC = nvcc

# --- Soporte CUDA (opcional) ---
# Uso: make CUDA=1           (Release con kernels CUDA)
#      make CUDA=1 DEBUG=1   (Debug con kernels CUDA)
# Sin CUDA=1, el build es exactamente igual que antes (sin dependencia de nvcc).
CUDA ?= 0

# Arquitectura GPU objetivo. sm_120 corresponde a RTX 50xx (Blackwell),
# requiere CUDA >= 12.8. Ajusta si compilas para otra GPU: sm_86 (RTX 30xx),
# sm_89 (RTX 40xx), etc.
CUDA_ARCH ?= sm_120
CUDA_HOME ?= /usr/local/cuda

# --- Idioma (en por defecto | es opcional) ---
HPSV_LANG ?= en

# Banderas base: C11 estándar, advertencias, OpenMP
CFLAGS_COMMON = -Wall -Wextra -std=c11 -fopenmp -D_POSIX_C_SOURCE=200809L \
                -D_DEFAULT_SOURCE -MMD -MP $(shell gdal-config --cflags)
LDFLAGS = -lm -lnetcdf -lpng -lwebp -fopenmp $(shell gdal-config --libs)

ifeq ($(CUDA),1)
    CFLAGS_COMMON += -DHPSV_CUDA -I$(CUDA_HOME)/include
    LDFLAGS += -L$(CUDA_HOME)/lib64 -lcudart -lstdc++
endif

# --- Flags de idioma ---
CFLAGS_LANG =

ifeq ($(HPSV_LANG),es)
    CFLAGS_LANG += -DHPSV_LANG_ES
    MANPAGE = man/hpsv.es.1
else
	MANPAGE = man/hpsv.1
endif

# --- Modos de Compilación ---
# Uso: make (Release por defecto) | make debug (para desarrollo con gdb)
ifdef DEBUG
    CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_LANG) -g -O0 -DDEBUG_MODE
    TARGET_NAME = hpsv_debug
else
    # Release: Optimización máxima (-O3) y nativa de la arquitectura
    CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_LANG) -O3 -march=native -funroll-loops
    TARGET_NAME = hpsv
endif

# --- Directorios ---
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
# Prefijo de instalación estándar (Linux)
PREFIX = /usr/local
MANDIR ?= $(PREFIX)/share/man

# --- Archivos ---
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Genera la lista de objetos esperados en obj/
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Fuentes CUDA (solo si CUDA=1)
CUDA_SRCS = $(wildcard $(SRC_DIR)/cuda/*.cu)
CUDA_OBJS = $(patsubst $(SRC_DIR)/cuda/%.cu, $(OBJ_DIR)/cuda/%.o, $(CUDA_SRCS))

ifeq ($(CUDA),1)
    OBJS += $(CUDA_OBJS)
    # Flags de nvcc según el modo de compilación (-G desactiva optimizaciones
    # del device code y habilita debug con cuda-gdb).
    ifdef DEBUG
        NVCCFLAGS = -std=c++14 -g -G
    else
        NVCCFLAGS = -std=c++14 -O3
    endif
endif

# El ejecutable final con ruta
TARGET = $(BIN_DIR)/$(TARGET_NAME)

# Inclusión de cabeceras
CFLAGS += -I$(INC_DIR)
DEPS = $(OBJS:.o=.d)

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

# Regla para compilar kernels CUDA (.cu -> .o)
# -MMD -MF genera el .d de dependencias (mismo mecanismo que gcc, requiere
# CUDA >= 11.2). -Xcompiler pasa flags al compilador de host que nvcc invoca.
$(OBJ_DIR)/cuda/%.o: $(SRC_DIR)/cuda/%.cu
	@echo "Compiling (nvcc) $<..."
	@$(NVCC) $(NVCCFLAGS) -arch=$(CUDA_ARCH) -DHPSV_CUDA -I$(INC_DIR) \
	         -MMD -MF $(@:.o=.d) -Xcompiler "-Wall -Wextra" -c $< -o $@

-include $(DEPS)

# Crear directorios si no existen
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/cuda
	@mkdir -p $(BIN_DIR)

# Limpieza
clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJ_DIR) $(BIN_DIR)

# Instalación (para el usuario final)
install: all
	@echo "Installing into $(PREFIX)/bin..."
	@install -d $(PREFIX)/bin
	@install -m 755 $(TARGET) $(PREFIX)/bin/hpsv
	@install -d $(MANDIR)/man1
	@install -m 644 $(MANPAGE) $(MANDIR)/man1/hpsv.1
	@echo "Installation successful."

uninstall:
	@rm -f $(PREFIX)/bin/hpsv
	@rm -f $(MANDIR)/man1/hpsv.1
	@echo "Uninstalled hpsv."

# Ayuda para debuggear el Makefile
info:
	@echo "Source files found: $(SRCS)"
	@echo "Object files target: $(OBJS)"
	@echo "Build mode: $(if $(DEBUG),debug,release)"
	@echo "Language: $(HPSV_LANG)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "Man Page: $(MANPAGE)"
