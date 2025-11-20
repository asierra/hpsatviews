# High Performance Satellite Views (HPSATVIEWS)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C11](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#building)

**Fast, reliable satellite image processing for GOES family satellites**

## Abstract

HPSATVIEWS is a high-performance, command-line driven application for processing GOES satellite data (L1b and L2). It provides a suite of tools to generate various imaging products, including true-color RGB composites, standard scientific products (`ash`, `airmass`, `so2`), single-channel grayscale images, and pseudocolor visualizations. Built in modern C11 with OpenMP support, it offers ultra-fast, modular, and scalable processing, making it ideal for operational meteorology and research.

## Resumen

HPSATVIEWS es una aplicación de alto rendimiento controlada por línea de comandos para el procesamiento de datos del satélite GOES (L1b y L2). Proporciona un conjunto de herramientas para generar diversos productos, incluyendo compuestos RGB de color verdadero, productos científicos estándar (`ash`, `airmass`, `so2`), imágenes de un solo canal en escala de grises y visualizaciones en pseudocolor. Desarrollado en C11 moderno con soporte para OpenMP, ofrece un procesamiento ultra-rápido, modular y escalable, ideal para la meteorología operacional y la investigación.

---

## 🚀 Características Principales

### Procesamiento de Imágenes
- **Compuestos RGB Avanzados**:
  - `truecolor`: Color verdadero diurno con canal verde sintético.
  - `ash`: Detección de ceniza volcánica.
  - `airmass`: Clasificación de masas de aire.
  - `so2`: Detección de dióxido de azufre.
  - `night`: Visualización infrarroja nocturna.
  - `composite`: Mezcla inteligente día/noche de `truecolor` y `night`.
- **Corrección Atmosférica de Rayleigh** - Eliminación de dispersión atmosférica en imágenes true color
  - Compatible con modos `truecolor` y `composite`
  - Implementación estándar siguiendo geo2grid/satpy
  - Corrección selectiva: aplica a C01 (Blue) y C02 (Red), pero NO a C03 (NIR)
  - Tablas LUT precalculadas para interpolación trilineal rápida
- **Mejora de Histograma** - Optimización automática de contraste
- **Corrección Gamma** - Control de luminosidad configurable (por defecto: 1.0, recomendado: 2.0 para visualización)
- **Reproyección Geográfica** - Conversión de proyección geoestacionaria a malla lat/lon uniforme
- **Recorte Geográfico** - Extracción de regiones de interés por coordenadas geográficas
  - Compatible con datos originales y reproyectados
  - Optimizado: recorta antes de reproyectar para máxima eficiencia

### Rendimiento
- ⚡ **Ultra rápido**: Procesamiento en fracciones de segundo
- 🔧 **Optimizado**: Código C11 compilado nativamente
- 🧵 **Paralelización**: Soporte OpenMP para procesamiento multi-core
- 💾 **Eficiente**: Gestión de memoria optimizada sin memory leaks

### Arquitectura de Software
- 🏗️ **Modular**: Arquitectura limpia con separación de responsabilidades
- 🔒 **Seguro en Hilos**: Sin variables globales, preparado para paralelización
- 📊 **Logging estructurado**: Sistema completo de debugging y monitoring
- 🛡️ **Gestión de memoria robusta**: Constructores/destructores automáticos
- 🧪 **Testeable**: Funciones aisladas y reutilizables

---

## 📋 Requisitos del Sistema

### Dependencias Requeridas
- **Compilador C11** (GCC recomendado)
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

## 🚀 Uso (Ejemplos)

El programa funciona con un único ejecutable `hpsatviews` y tres subcomandos principales: `rgb`, `pseudocolor` y `singlegray`.

### Comando `rgb`

Genera compuestos RGB a partir de múltiples canales. El archivo de entrada puede ser cualquier canal (L1b o L2) del instante de tiempo deseado; el programa encontrará los demás automáticamente.

**Compuesto Día/Noche (por defecto):**
```bash
./hpsatviews rgb -o composite.png /ruta/a/OR_ABI-L1b-RadF-M6C02_G16...
```

**Genera:**
- `dia.png` - Imagen true color RGB
- `noche.png` - Imagen infrarroja con pseudocolor
- `mask.png` - Máscara día/noche
- `out.png` - Composición final automática

**Modos RGB disponibles:**
```bash
# True color diurno
./hpsatviews rgb -m truecolor -o salida.png archivo.nc

# True color con corrección atmosférica de Rayleigh (recomendado)
./hpsatviews rgb -m truecolor --rayleigh -g 2 -o salida.png archivo.nc

# Detección de ceniza volcánica
./hpsatviews rgb -m ash -o ceniza.png archivo.nc

# Clasificación de masas de aire
./hpsatviews rgb -m airmass -o airmass.png archivo.nc

# Detección de SO2
./hpsatviews rgb -m so2 -o so2.png archivo.nc

# Visualización nocturna
./hpsatviews rgb -m night -o night.png archivo.nc

# Composición día/noche con Rayleigh
./hpsatviews rgb -m composite --rayleigh -g 2 -o composite.png archivo.nc
```

**Reproyección Geográfica:**
```bash
# Reproyectar a malla lat/lon uniforme
./hpsatviews rgb -m ash -r -o reproyectado.png archivo.nc
```

**Recorte Geográfico:**
```bash
# Recortar región de interés (sin reproyección)
./hpsatviews rgb -m ash --clip -107.23 22.72 -93.84 14.94 -o recorte.png archivo.nc

# Recortar Y reproyectar (orden optimizado: recorta primero, luego reproyecta)
./hpsatviews rgb -m ash --clip -107.23 22.72 -93.84 14.94 -r -o recorte_reproj.png archivo.nc
```

**Formato del recorte:** `--clip lon_min lat_max lon_max lat_min`
- Coordenadas en grados decimales
- Longitud oeste es negativa
- Ejemplo: CONUS central: `--clip -107.23 22.72 -93.84 14.94`

**Opciones adicionales:**
- `--rayleigh` - Aplicar corrección atmosférica de Rayleigh (solo modos truecolor/composite)
- `-g, --gamma <valor>` - Corrección gamma (por defecto: 1.0, recomendado con Rayleigh: 2.0)
- `-v, --verbose` - Modo verboso con logging detallado
- `-o, --out <archivo>` - Nombre del archivo de salida

### Imagen en Escala de Grises
```bash
./hpsatviews singlegray archivo_GOES_L1b.nc -o salida.png
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
├── 🗺️ reprojection.h/.c  # Reproyección geoestacionaria a geográfica
├── 🎨 rgb.h/.c           # Generación de compuestos RGB
├── 🌅 truecolor_rgb.c    # Generación de imágenes RGB true color
├── ☁️ rayleigh.h/.c       # Corrección atmosférica de Rayleigh
├── 📊 rayleigh_lut.h/.c  # Manejo de tablas LUT para Rayleigh
├── 🌙 nocturnal_pseudocolor.c # Imágenes infrarrojas nocturnas
├── 🌗 daynight_mask.c    # Cálculo de máscara día/noche
├── ⚙️ args.h/.c          # Procesamiento de argumentos
├── 📁 rayleigh_lut_C*.bin # Tablas LUT precalculadas (C01, C02)
└── 📖 README.md          # Este archivo
```

---

## 🔍 Datos de Entrada

### Formato Soportado
- **GOES-16/17/18 Level 1b NetCDF** (Radiance data)
- **GOES-16/17/18 Level 2 NetCDF** (CMI - Cloud and Moisture Imagery)
- Canales principales: C01 (0.47μm), C02 (0.64μm), C03 (0.86μm), C11-C16 (IR)
- Proyecciones: Geoestacionaria GOES (nativa) y Geográfica lat/lon (reproyectada)

### Ejemplo de Nombres de Archivo
```
# Level 1b (Radiance)
OR_ABI-L1b-RadC-M6C01_G16_s20242501800_e20242501809_c20242501815.nc
OR_ABI-L1b-RadF-M6C02_G16_s20242501800_e20242501809_c20242501815.nc

# Level 2 (CMI - Cloud and Moisture Imagery)
OR_ABI-L2-CMIPC-M3C13_G16_s20190871342161_e20190871344546_c20190871344589.nc
OR_ABI-L2-CMIPF-M6C02_G16_s20243102000217_e20243102009525_c20243102010008.nc
```

---

## 🏗️ Construcción y Desarrollo

### Targets de Makefile
```bash
make                    # Construir todo
make clean             # Limpiar archivos objeto
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
2. **Downsampling** de C01, C02, C03 a resolución común (2km)
3. **Canal verde sintético** usando coeficientes EDC: `0.45706946*C01 + 0.48358168*C02 + 0.06038137*C03`
4. **Normalización radiométrica** con factores de escala NetCDF
5. **Corrección gamma** para visualización óptima (recomendado: 2.0)
6. **Mejora de histograma** opcional

### Corrección Atmosférica de Rayleigh
1. **Cálculo de geometría solar**: SZA (Solar Zenith Angle) y SAA (Solar Azimuth Angle)
2. **Cálculo de geometría del satélite**: VZA (View Zenith Angle) y VAA (View Azimuth Angle)
3. **Cálculo de azimut relativo**: RAA = |SAA - VAA| normalizado a [0, 180]
4. **Interpolación trilinear en LUT**: Busca valor de Rayleigh para (SZA, VZA, RAA)
5. **Aplicación selectiva**:
   - ✅ **C01 (Blue)**: Corrección aplicada (más afectado por dispersión Rayleigh)
   - ✅ **C02 (Red)**: Corrección aplicada  
   - ❌ **C03 (NIR)**: SIN corrección (dispersión Rayleigh es despreciable en NIR)
6. **Verde sintético con canales corregidos**: Combina C01/C02 corregidos + C03 original
7. **Enmascaramiento nocturno**: Píxeles con SZA > 88° se marcan como noche (valor 0)
8. **Actualización de rangos**: Recalcula fmin/fmax después de corrección para normalización correcta

**Estándar seguido**: Implementación compatible con geo2grid/satpy para resultados científicos reproducibles.

### Visualización Infrarroja Nocturna
1. **Conversión radiancia a temperatura** usando ecuación de Planck
2. **Mapeo de color meteorológico** para temperaturas de tope de nube
3. **Mejora de contraste** para estructuras atmosféricas

### Composición Día/Noche
1. **Cálculo de ángulo solar zenital** para cada píxel
2. **Generación de máscara** basada en geometría solar
3. **Mezcla ponderada** entre imágenes diurna y nocturna

### Reproyección Geográfica
1. **Cálculo de navegación** desde metadatos GOES (fixed grid projection)
2. **Mapeo forward** de píxeles geoestacionarios a malla lat/lon
3. **Interpolación de huecos** con vecinos más cercanos (4-conectividad)
4. **Optimización**: Pre-cálculo de factores de escala para máximo rendimiento

### Recorte Geográfico Inteligente
1. **Sin reproyección**: Búsqueda de píxeles más cercanos a coordenadas objetivo
2. **Con reproyección**: 
   - Recorta primero en espacio geoestacionario (orden optimizado)
   - Reproyecta solo el área recortada
   - Cálculo automático de límites geográficos del área recortada
   - Resultado: Máxima calidad con mínimo tiempo de procesamiento

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
// Cargar datos NetCDF (funciona con L1b y L2)
DataNC channel;
load_nc_sf("archivo.nc", "Rad", &channel);  // L1b
load_nc_sf("archivo.nc", "CMI", &channel);  // L2

// Remuestreo
DataF downsampled = downsample_boxfilter(channel.base, factor);
DataF upsampled = upsample_bilinear(channel.base, factor);

// Recorte de regiones
DataF cropped = dataf_crop(&data, x_start, y_start, width, height);
```

### Reproyección y Navegación
```c
// Calcular navegación desde archivo NetCDF
DataF navla, navlo;
compute_navigation_nc("archivo.nc", &navla, &navlo);

// Reproyectar a geográfica
DataF reproj = reproject_to_geographics(&data, "archivo_nav.nc", 
                                        &lon_min, &lon_max, &lat_min, &lat_max);

// Reproyectar con navegación pre-calculada (más eficiente)
DataF reproj = reproject_to_geographics_with_nav(&data, &navla, &navlo,
                                                  &lon_min, &lon_max, &lat_min, &lat_max);

// Buscar píxel más cercano a coordenada geográfica
int x, y;
reprojection_find_pixel_for_coord(&navla, &navlo, target_lat, target_lon, &x, &y);
```

### Geometría Solar y de Satélite (para Corrección Rayleigh)
```c
// 1. Calcular navegación (lat/lon) desde archivo NetCDF
DataF navla, navlo;
compute_navigation_nc("OR_ABI-L1b-RadF-M6C01_G19_s2025....nc", &navla, &navlo);

// 2. Calcular ángulos solares (Solar Zenith y Azimuth)
DataF sza, saa;  // Solar Zenith Angle, Solar Azimuth Angle
compute_solar_angles_nc("OR_ABI-L1b-RadF-M6C01_G19_s2025....nc", 
                        &navla, &navlo, &sza, &saa);

// 3. Calcular ángulos del satélite (View Zenith y Azimuth)
DataF vza, vaa;  // View Zenith Angle, View Azimuth Angle
compute_satellite_angles_nc("OR_ABI-L1b-RadF-M6C01_G19_s2025....nc",
                            &navla, &navlo, &vza, &vaa);

// 4. Calcular azimut relativo (diferencia entre sol y satélite)
DataF raa;  // Relative Azimuth Angle
compute_relative_azimuth(&saa, &vaa, &raa);

// 5. Aplicar corrección atmosférica de Rayleigh (cuando esté disponible la LUT)
// RayleighLUT lut = rayleigh_lut_load("rayleigh_lut_C01.bin");
// apply_rayleigh_correction(&reflectance_image, &sza, &vza, &raa, &lut);
// rayleigh_lut_destroy(&lut);

// Liberar memoria
dataf_destroy(&navla);
dataf_destroy(&navlo);
dataf_destroy(&sza);
dataf_destroy(&saa);
dataf_destroy(&vza);
dataf_destroy(&vaa);
dataf_destroy(&raa);
```

---

## 📊 Rendimiento

### Benchmarks Típicos (Procesamiento RGB)
- **Imagen 5424x5424**: ~0.5 segundos
- **Imagen 2712x2712**: ~0.2 segundos  
- **Composición completa**: ~1.0 segundo
- **Reproyección 2500x1500**: ~0.3 segundos
- **Recorte + Reproyección (~660x400)**: ~0.1 segundos

### Optimizaciones Implementadas
- ✅ **Paralelización OpenMP** en bucles críticos
- ✅ **Pre-cálculo de factores** para evitar divisiones repetidas
- ✅ **Recorte inteligente** antes de reproyección (evita procesar píxeles innecesarios)
- ✅ **Operaciones atómicas** para escritura thread-safe sin locks
- ✅ **Gestión eficiente de memoria** con destructores automáticos

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
- **C11** estándar
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
