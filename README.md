# High Performance Satellite Views (HPSATVIEWS)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C99](https://img.shields.io/badge/C-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#building)

**Fast, reliable satellite image processing for GOES family satellites**

## Abstract

HPSATVIEWS is a high-performance satellite image processing library and application suite designed for real-time operational visualization of GOES family satellite data. The system generates true color daytime images, infrared nighttime visualizations with meteorological colormaps, and intelligent day/night composites. Built in modern C99, it processes satellite data in fractions of a second compared to minutes required by Python-based alternatives, making it ideal for operational meteorological applications.

## Resumen

HPSATVIEWS es un sistema de procesamiento de imágenes satelitales de alto rendimiento diseñado para la familia de satélites GOES. Genera imágenes a color real para el día, visualizaciones infrarrojas nocturnas con mapas de color meteorológicos, y composiciones inteligentes día/noche. Desarrollado en C99 moderno, procesa datos satelitales en fracciones de segundo comparado con los minutos requeridos por herramientas basadas en Python.

---

## 🚀 Características Principales

### Procesamiento de Imágenes
- **Imágenes True Color RGB** - Combinación automática de canales C01, C02, C03
- **Visualización Infrarroja Nocturna** - Pseudocolor meteorológico del canal C13
- **Composición Día/Noche** - Mezcla inteligente basada en geometría solar
- **Mejora de Histograma** - Optimización automática de contraste
- **Corrección Gamma** - Control de luminosidad configurable

### Rendimiento
- ⚡ **Ultra rápido**: Procesamiento en fracciones de segundo
- 🔧 **Optimizado**: Código C99 compilado nativamente
- 🧵 **Paralelización**: Soporte OpenMP para procesamiento multi-core
- 💾 **Eficiente**: Gestión de memoria optimizada sin memory leaks

### Arquitectura de Software
- 🏗️ **Modular**: Arquitectura limpia con separación de responsabilidades
- 🔒 **Thread-safe**: Sin variables globales, preparado para paralelización
- 📊 **Logging estructurado**: Sistema completo de debugging y monitoring
- 🛡️ **Gestión de memoria robusta**: Constructores/destructores automáticos
- 🧪 **Testeable**: Funciones aisladas y reutilizables

---

## 📋 Requisitos del Sistema

### Dependencias Requeridas
- **Compilador C99** (GCC recomendado)
- **libnetcdf-dev** - Lectura de archivos NetCDF GOES L1b
- **libpng-dev** - Generación de imágenes PNG
- **libm** - Funciones matemáticas
- **OpenMP** (opcional) - Paralelización

### Sistemas Operativos Soportados
- Linux (Ubuntu, CentOS, RHEL)
- macOS (con Homebrew)
- Windows (con MSYS2/MinGW)

---

## 🔧 Instalación

### Ubuntu/Debian
```bash
# Instalar dependencias
sudo apt update
sudo apt install build-essential libnetcdf-dev libpng-dev

# Clonar repositorio
git clone https://github.com/asierra/hpsatviews.git
cd hpsatviews

# Compilar
make
```

### CentOS/RHEL
```bash
# Instalar dependencias
sudo yum install gcc netcdf-devel libpng-devel

# Compilar
make
```

### macOS
```bash
# Instalar dependencias
brew install netcdf libpng

# Compilar
make
```

---

## 🚀 Uso

El programa ahora funciona con un único ejecutable `hpsatviews` y subcomandos: `rgb`, `pseudocolor`, y `singlegray`.

### Generar Imagen RGB (Color Verdadero + Composición Día/Noche)
```bash
./hpsatviews rgb -o truecolor_comp.png /ruta/a/archivo_GOES_L1b_C02.nc
```

**Genera:**
- `dia.png` - Imagen true color RGB
- `noche.png` - Imagen infrarroja con pseudocolor
- `mask.png` - Máscara día/noche
- `out.png` - Composición final automática

### Imagen en Escala de Grises
```bash
./singlegray archivo_GOES_L1b.nc -o salida.png
```

**Opciones disponibles:**
- `-i, --invert` - Invertir valores
- `-h, --histo` - Aplicar mejora de histograma
- `-g, --gamma <valor>` - Corrección gamma
- `-s, --scale <factor>` - Factor de escalado
- `-a, --alpha` - Canal alpha para transparencia

---

## 📁 Estructura del Proyecto

```
hpsatviews/
├── 📄 main.c              # Programa principal
├── 📄 singlegraymain.c    # Utilidad escala de grises
├── 📚 libhpsatviews.a     # Biblioteca principal
├── 🔧 Makefile           # Sistema de construcción
├── 📊 logger.h/.c        # Sistema de logging
├── 🖼️ image.h/.c         # Estructuras y manipulación de imágenes
├── 📡 reader_nc.h/.c     # Lectura de archivos NetCDF GOES
├── 💾 writer_png.h/.c    # Escritura de archivos PNG
├── 🌈 datanc.h/.c        # Estructuras de datos y algoritmos
├── 🌅 truecolor_rgb.c    # Generación de imágenes RGB
├── 🌙 nocturnal_pseudocolor.c # Imágenes infrarrojas nocturnas
├── 🌗 daynight_mask.c    # Cálculo de máscara día/noche
├── ⚙️ args.h/.c          # Procesamiento de argumentos
└── 📖 README.md          # Este archivo
```

---

## 🔍 Datos de Entrada

### Formato Soportado
- **GOES-16/17 Level 1b NetCDF** 
- Canales requeridos: C01 (0.47μm), C02 (0.64μm), C03 (0.86μm), C13 (10.3μm)
- Proyección: Geoestacionaria GOES

### Ejemplo de Nombres de Archivo
```
OR_ABI-L1b-RadC01_G16_s20242501800_e20242501809_c20242501815.nc
OR_ABI-L1b-RadC02_G16_s20242501800_e20242501809_c20242501815.nc
OR_ABI-L1b-RadC03_G16_s20242501800_e20242501809_c20242501815.nc
OR_ABI-L1b-RadC13_G16_s20242501800_e20242501809_c20242501815.nc
```

---

## 🏗️ Construcción y Desarrollo

### Targets de Makefile
```bash
make                    # Construir todo
make clean             # Limpiar archivos objeto
make truecolornight    # Programa principal
make singlegray        # Utilidad escala de grises
make libhpsatviews.a   # Solo la biblioteca
```

### Configuración de Logging
```c
#include "logger.h"

// Inicializar con nivel INFO
logger_init(LOG_INFO);

// Habilitar logging a archivo
logger_set_file("hpsatviews.log");

// Usar en el código
LOG_INFO("Procesando archivo: %s", filename);
LOG_ERROR("Error al abrir archivo: %s", error_msg);
```

---

## 🔬 Algoritmos y Metodología

### Procesamiento True Color RGB
1. **Lectura de canales** C01 (azul), C02 (rojo), C03 (vegetal)
2. **Normalización radiométrica** con factores de escala NetCDF
3. **Corrección gamma** para visualización óptima
4. **Mejora de histograma** opcional

### Visualización Infrarroja Nocturna
1. **Conversión radiancia a temperatura** usando ecuación de Planck
2. **Mapeo de color meteorológico** para temperaturas de tope de nube
3. **Mejora de contraste** para estructuras atmosféricas

### Composición Día/Noche
1. **Cálculo de ángulo solar zenital** para cada píxel
2. **Generación de máscara** basada en geometría solar
3. **Mezcla ponderada** entre imágenes diurna y nocturna

---

## 🛠️ API de Desarrollo

### Gestión de Memoria
```c
// Crear estructura de datos
DataF data = dataf_create(width, height);
ImageData image = image_create(width, height, bpp);

// Liberar automáticamente
dataf_destroy(&data);
image_destroy(&image);
```

### Procesamiento de Canales
```c
// Cargar datos NetCDF
DataNC channel;
load_nc_sf("archivo.nc", "Rad", &channel);

// Remuestreo
DataF downsampled = downsample_boxfilter(channel.base, factor);
DataF upsampled = upsample_bilinear(channel.base, factor);
```

---

## 📊 Rendimiento

### Benchmarks Típicos
- **Imagen 5424x5424**: ~0.5 segundos
- **Imagen 2712x2712**: ~0.2 segundos  
- **Composición completa**: ~1.0 segundo

### Comparación con Python
- **HPSATVIEWS**: 0.5-1.0 segundos
- **Python equivalente**: 30-120 segundos
- **Mejora**: 30-120x más rápido

---

## 🤝 Contribuciones

Las contribuciones son bienvenidas. Por favor:

1. Fork el proyecto
2. Crear branch para feature (`git checkout -b feature/nueva-funcionalidad`)
3. Commit cambios (`git commit -am 'Añadir nueva funcionalidad'`)
4. Push al branch (`git push origin feature/nueva-funcionalidad`)
5. Crear Pull Request

### Estándares de Código
- **C99** estándar
- **Logging estructurado** para debugging
- **Gestión de memoria** robusta con constructores/destructores
- **Sin variables globales** (thread-safe)
- **Documentación** en código y commits

---

## 📝 Licencia

```
Copyright (c) 2025 Alejandro Aguilar Sierra (asierra@unam.mx)
Laboratorio Nacional de Observación de la Tierra, UNAM

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

Consulta el archivo [LICENSE](LICENSE) para más detalles.

---

## 👨‍💻 Autor

**Alejandro Aguilar Sierra**  
📧 asierra@unam.mx  
🏛️ Laboratorio Nacional de Observación de la Tierra, UNAM  
🔗 [GitHub](https://github.com/asierra)

---

## 🙏 Agradecimientos

- NOAA por los datos GOES-16/17
- Comunidad NetCDF por las bibliotecas de acceso a datos
- Desarrolladores de libpng por el procesamiento de imágenes

---

*HPSATVIEWS - Procesamiento satelital de alta velocidad para meteorología operacional*
