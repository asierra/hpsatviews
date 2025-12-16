# High Performance Satellite Views (HPSATVIEWS)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C11](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#building)

**Fast, reliable satellite image processing for GOES family satellites**

## Abstract

HPSATVIEWS is a high-performance, command-line driven application for processing GOES satellite data (L1b and L2). It provides a suite of tools to generate various imaging products, including true-color RGB composites, standard scientific products (`ash`, `airmass`, `so2`), single-channel grayscale images, and pseudocolor visualizations. Built in modern C11 with OpenMP support, it offers ultra-fast, modular, and scalable processing, making it ideal for operational meteorology and research.

**Supported satellites:** GOES-16, GOES-18, GOES-19 (operational), and GOES-17 (historical data).

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
  - `night`: Visualización infrarroja nocturna con pseudocolor y luces de ciudad.
  - `composite`: Mezcla inteligente día/noche de `truecolor` y `night` (con luces de ciudad automáticas).
- **Resampling Automático Inteligente** - Gestión de canales con diferentes resoluciones.
  - **Rápido por defecto**: Usa el canal de menor resolución como referencia y aplica *downsampling* a los demás. Ideal para vistas previas rápidas.
  - **Máxima calidad opcional**: Con la bandera `--full-res`, usa el canal de mayor resolución como referencia y aplica *upsampling* a los demás para preservar el máximo detalle.
  - Ejemplo (defecto): C01(1km) + C02(0.5km) + C03(2km) → todos a 2km.
  - Ejemplo (`--full-res`): C01(1km) + C02(0.5km) + C03(1km) → todos a 0.5km.
- **Patrones de Nombre de Archivo** - Expansión automática de metadatos en nombres de salida
  - Patrones de fecha/hora: `{YYYY}`, `{MM}`, `{DD}`, `{hh}`, `{mm}`, `{ss}`, `{JJJ}`, `{YY}`
  - Patrones de metadatos: `{CH}` (canal/banda), `{SAT}` (satélite)
  - Ejemplo: `test_{SAT}_{CH}_{YYYY}{MM}{DD}.png` → `test_goes-16_C01_20240807.png`
- **Corrección Atmosférica de Rayleigh** - Eliminación de dispersión atmosférica en imágenes true color
  - Compatible con modos `truecolor` y `composite`
  - Implementación estándar siguiendo geo2grid/satpy
  - Corrección selectiva: aplica a C01 (Blue) y C02 (Red), pero NO a C03 (NIR)
  - Tablas LUT embebidas en el ejecutable para máximo rendimiento (sin I/O en disco)
- **Mejora de Histograma** - Optimización automática de contraste
- **CLAHE (Contrast Limited Adaptive Histogram Equalization)** - Ecualización adaptativa con control de contraste local
  - Divide la imagen en grilla de tiles para procesamiento local
  - Parámetros configurables: grid (tiles_x, tiles_y) y clip_limit
  - Interpolación bilinear entre tiles para evitar artefactos de bloques
  - Paralelización OpenMP para máximo rendimiento
  - Superior a ecualización global en imágenes con variaciones locales de contraste
- **Corrección Gamma** - Control de luminosidad configurable (por defecto: 1.0, recomendado: 2.0 para visualización)
- **Reproyección Geográfica** - Conversión de proyección geoestacionaria a malla lat/lon uniforme
- **Recorte Geográfico** - Extracción de regiones de interés por coordenadas geográficas
  - Compatible con datos originales y reproyectados
  - Optimizado: recorta antes de reproyectar para máxima eficiencia
  - Inferencia inteligente de esquinas cuando el dominio se extiende fuera del disco visible

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
- **libgdal-dev** - Generación de imágenes GeoTIFF
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
sudo apt install build-essential libnetcdf-dev libpng-dev libgdal-dev

# Clonar repositorio
git clone https://github.com/asierra/hpsatviews.git
cd hpsatviews

# Compilar
make
```

### CentOS/RHEL
```bash
# Instalar dependencias
sudo yum install gcc netcdf-devel libpng-devel gdal-devel

# Compilar
make
```

### macOS
```bash
# Instalar dependencias
brew install netcdf libpng gdal

# Compilar
make
```

---

## 📁 Formatos de Salida

### PNG (Predeterminado)
Formato de imagen rasterizada sin georreferenciación. Ideal para visualización rápida y distribución web.

### GeoTIFF (Georreferenciado)
Formato TIFF con metadatos de proyección completos, compatible con QGIS, GDAL, ArcGIS, etc.

**Activación**:
- **Explícita**: Flag `-t` o `--tif`
- **Automática**: Extensión `.tif` en nombre de salida

**Ejemplos**:
```bash
# Opción explícita
./hpsatviews rgb -m truecolor -t -o salida.tif archivo.nc

# Detección automática por extensión
./hpsatviews rgb -m truecolor -o salida.tif archivo.nc

# PNG (sin -t y extensión .png)
./hpsatviews rgb -m truecolor -o salida.png archivo.nc
```

**Proyecciones Soportadas**:
- **PROJ_GEOS**: Proyección geoestacionaria nativa del satélite (sin `-r`)
- **PROJ_LATLON**: Proyección geográfica ecuirectangular (con `-r`)

Ambas incluyen metadatos completos (WKT, GeoTransform) para correcta georreferenciación.

**Compatibilidad**:
GeoTIFF es compatible con todas las opciones: `--clip`, `-r`, `--rayleigh`, `-g`, `-h`, etc.

---

## 🚀 Uso (Ejemplos)

El programa funciona con un único ejecutable `hpsatviews` y tres subcomandos principales: `rgb`, `pseudocolor` y `singlegray`.

### Ver Ayuda y Recortes Disponibles

```bash
# Ayuda general
./hpsatviews --help

# Listar recortes geográficos predefinidos
./hpsatviews --list-clips

# Ayuda de un comando específico
./hpsatviews rgb --help
```

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

# True color con CLAHE para mejorar contraste local (usa defaults: 8,8,4.0)
./hpsatviews rgb -m truecolor --rayleigh -g 2 --clahe -o salida.png archivo.nc

# True color con CLAHE personalizado (--clahe-params activa CLAHE automáticamente)
./hpsatviews rgb -m truecolor --rayleigh -g 2 --clahe-params "16,16,5.0" -o salida.png archivo.nc

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

El recorte geográfico soporta dos formatos:

1. **Clave predefinida** (recomendado):
```bash
# Usar un recorte predefinido por su clave
./hpsatviews rgb -m ash -c mexico -o recorte.png archivo.nc

# Ver claves disponibles
./hpsatviews --list-clips

# Ejemplos de claves disponibles: mexico, local, caribe, a1, a2, etc.
```

2. **Coordenadas directas**:
```bash
# Con comas (sin comillas)
./hpsatviews rgb -m ash -c -107.23,22.72,-93.84,14.94 -o recorte.png archivo.nc

# Con espacios (CON comillas)
./hpsatviews rgb -m ash -c "-107.23 22.72 -93.84 14.94" -o recorte.png archivo.nc
```

**Recorte + Reproyección:**
```bash
# Usando clave predefinida (orden optimizado: recorta primero, luego reproyecta)
./hpsatviews rgb -m ash -c mexico -r -o recorte_reproj.png archivo.nc

# Usando coordenadas
./hpsatviews rgb -m ash -c -107.23,22.72,-93.84,14.94 -r -o recorte_reproj.png archivo.nc
```

**Formato del recorte con coordenadas:** `lon_min,lat_max,lon_max,lat_min` o `"lon_min lat_max lon_max lat_min"`
- Coordenadas en grados decimales
- Longitud oeste es negativa
- Ejemplo: CONUS central: `-107.23,22.72,-93.84,14.94`
- **Nota**: Para dominios amplios que se extienden más allá del disco visible del satélite, las esquinas fuera del disco se infieren automáticamente usando geometría rectangular, garantizando recortes precisos incluso cuando parte del dominio no es visible desde el satélite

**Formato de Salida:**
```bash
# PNG (por defecto)
./hpsatviews rgb -m truecolor -o salida.png archivo.nc

# GeoTIFF georreferenciado (opción -t o extensión .tif)
./hpsatviews rgb -m truecolor -t -o salida.tif archivo.nc
./hpsatviews rgb -m truecolor -o salida.tif archivo.nc  # Detecta automáticamente
```

**Patrones de Nombre de Archivo:**

El parámetro `-o/--out` soporta expansión automática de patrones extrayendo información del nombre del archivo de entrada:

| Patrón | Descripción | Ejemplo |
|--------|-------------|---------|
| `{YYYY}` | Año 4 dígitos | 2024 |
| `{YY}` | Año 2 dígitos | 24 |
| `{MM}` | Mes (01-12) | 08 |
| `{DD}` | Día (01-31) | 07 |
| `{JJJ}` | Día juliano (001-366) | 220 |
| `{hh}` | Hora (00-23) | 18 |
| `{mm}` | Minuto (00-59) | 01 |
| `{ss}` | Segundo (00-59) | 17 |
| `{CH}` | Número de banda/canal | C01, C02, C13 |
| `{SAT}` | Nombre del satélite | goes-16, goes-18, goes-19 |

```bash
# Con patrones de fecha/hora
./hpsatviews rgb -o "truecolor_{YYYY}-{MM}-{DD}_{hh}:{mm}.png" \
  OR_ABI-L1b-RadF-M6C01_G16_s20242190300217_e20242190309525_c20242190310008.nc
# → truecolor_2024-08-07_18:01.png

# Con patrones de satélite y canal
./hpsatviews rgb -o "test_{SAT}_{CH}_{YYYY}{MM}{DD}_{hh}{mm}.png" \
  OR_ABI-L1b-RadF-M6C01_G16_s20242190300217_e20242190309525_c20242190310008.nc \
  OR_ABI-L1b-RadF-M6C02_G16_s20242190300217_e20242190309525_c20242190310008.nc \
  OR_ABI-L1b-RadF-M6C03_G16_s20242190300217_e20242190309525_c20242190310008.nc
# → test_goes-16_C01_20240807_1801.png

# Funciona con todos los comandos
./hpsatviews singlegray -o "ir_{SAT}_{CH}_{YYYY}{MM}{DD}.png" \
  OR_ABI-L1b-RadF-M6C13_G16_s20242190300217_e20242190309525_c20242190310008.nc
# → ir_goes-16_C13_20240807.png

# Con GeoTIFF
./hpsatviews pseudocolor -p paleta.cpt -o "ash_{SAT}_band{CH}_{YYYY}-{MM}-{DD}_{hh}:{mm}:{ss}.tif" \
  OR_ABI-L1b-RadF-M6C01_G16_s20242190300217_e20242190309525_c20242190310008.nc
# → ash_goes-16_bandC01_2024-08-07_18:01:17.tif
```

**Opciones específicas del comando rgb:**
- `-m, --mode <modo>` - Modo de operación: `composite` (defecto), `truecolor`, `night`, `ash`, `airmass`, `so2`
- `--rayleigh` - Aplicar corrección atmosférica de Rayleigh (solo truecolor/composite)

**Opciones comunes:** Ver sección "Estandarización de Opciones" más abajo.

### Comando `pseudocolor`

Genera imágenes con paleta de colores a partir de un solo canal.

```bash
./hpsatviews pseudocolor -p paleta.cpt archivo_GOES.nc -o salida.png
```

**Opciones específicas del comando pseudocolor:**
- `-p, --cpt <archivo>` - Archivo de paleta de colores (.cpt) - **Requerido**

**Opciones comunes:** Ver sección "Estandarización de Opciones" más abajo.

**Nota:** La opción `--invert` fue eliminada de pseudocolor (no tiene sentido con paletas de colores).

### Comando `singlegray`

Genera imágenes en escala de grises a partir de un solo canal.

```bash
./hpsatviews singlegray archivo_GOES_L1b.nc -o salida.png
```

**Opciones específicas del comando singlegray:**
- `-i, --invert` - Invertir valores (blanco ↔ negro)

**Opciones comunes:** Ver sección "Estandarización de Opciones" más abajo.

### Estandarización de Opciones (Diciembre 2025)

Los tres comandos (`rgb`, `pseudocolor`, `singlegray`) comparten ahora un conjunto consistente de opciones:

**Opciones comunes:**
- `-o, --out` - Archivo de salida (PNG o GeoTIFF según extensión)
- `-t, --tif` - Generar GeoTIFF georreferenciado
- `-c, --clip` - Recorte geográfico (clave predefinida o coordenadas numéricas)
- `-g, --gamma` - Corrección gamma
- `-h, --histo` - Ecualización de histograma global
- `--clahe` - CLAHE (ecualización adaptativa) con parámetros por defecto (8,8,4.0)
- `--clahe-params <params>` - Parámetros CLAHE personalizados: "tiles_x,tiles_y,clip_limit" (activa --clahe automáticamente)
- `-s, --scale` - Factor de escalado
- `-a, --alpha` - Canal alfa
- `-r, --geographics` - Reproyección geográfica
- `-v, --verbose` - Logging detallado

**Opciones globales:**
- `--list-clips` - Muestra los recortes geográficos predefinidos disponibles (sale inmediatamente)

**Opciones exclusivas:**
- `rgb`: `-m/--mode` (modo de composición), `--rayleigh` (corrección atmosférica)
- `pseudocolor`: `-p/--cpt` (paleta de colores)
- `singlegray`: `-i/--invert` (inversión blanco/negro)

Esta estandarización mejora la consistencia de la interfaz y facilita el aprendizaje del uso del programa.

---

## 📁 Estructura del Proyecto

```
hpsatviews/
├── � include/                    # Headers públicos (.h)
│   ├── args.h                     # Procesamiento de argumentos
│   ├── channelset.h               # Gestión de conjuntos de canales
│   ├── clip_loader.h              # Carga de recortes predefinidos
│   ├── datanc.h                   # Estructuras de datos y algoritmos
│   ├── daynight_mask.h            # Máscara día/noche
│   ├── filename_utils.h           # Utilidades de nombres de archivo
│   ├── image.h                    # Estructuras de imágenes
│   ├── logger.h                   # Sistema de logging
│   ├── nocturnal_pseudocolor.h    # Pseudocolor nocturno
│   ├── paleta.h                   # Definiciones de paletas
│   ├── processing.h               # Pipeline singlegray/pseudocolor
│   ├── rayleigh.h                 # Corrección Rayleigh
│   ├── rayleigh_lut_embedded.h    # LUTs embebidas
│   ├── reader_cpt.h               # Lector de paletas CPT
│   ├── reader_nc.h                # Lector NetCDF
│   ├── reader_png.h               # Lector PNG
│   ├── reprojection.h             # Reproyección geográfica
│   ├── rgb.h                      # Pipeline RGB multicanal
│   ├── singlegray.h               # Módulo singlegray
│   ├── truecolor.h                # Auxiliares true color
│   ├── writer_geotiff.h           # Escritor GeoTIFF
│   └── writer_png.h               # Escritor PNG
│
├── 📂 src/                        # Código fuente (.c)
│   ├── main.c                     # Programa principal
│   ├── args.c                     # Parseo de argumentos CLI
│   ├── channelset.c               # Gestión multi-resolución
│   ├── clip_loader.c              # Carga de recortes CSV
│   ├── datanc.c                   # Operaciones sobre datos
│   ├── daynight_mask.c            # Cálculo de máscara solar
│   ├── filename_utils.c           # Expansión de patrones {CH}, {SAT}, etc.
│   ├── image.c                    # Manipulación de imágenes (CLAHE, gamma)
│   ├── logger.c                   # Logging estructurado
│   ├── nocturnal_pseudocolor.c    # Visualización infrarroja nocturna
│   ├── processing.c               # Pipeline singlegray/pseudocolor
│   ├── rayleigh.c                 # Corrección atmosférica
│   ├── rayleigh_lut_embedded.c    # LUTs compiladas
│   ├── reader_cpt.c               # Lectura de paletas GMT
│   ├── reader_nc.c                # Lectura NetCDF + metadatos
│   ├── reader_png.c               # Lectura PNG
│   ├── reprojection.c             # Geoestacionaria → Geográfica
│   ├── rgb.c                      # Compuestos RGB (composite, truecolor, ash, etc.)
│   ├── singlegray.c               # Escala de grises
│   ├── truecolor_rgb.c            # True color + verde sintético
│   ├── writer_geotiff.c           # Salida GeoTIFF georreferenciada
│   └── writer_png.c               # Salida PNG
│
├── 📂 sample_data/                # Datos de ejemplo GOES-16 L2 CMI
│   ├── OR_ABI-L2-CMIPC-M6C01_G16...nc  # Canal 01 (Blue, 1km)
│   ├── OR_ABI-L2-CMIPC-M6C02_G16...nc  # Canal 02 (Red, 500m)
│   ├── OR_ABI-L2-CMIPC-M6C03_G16...nc  # Canal 03 (Veggie, 1km)
│   ├── OR_ABI-L2-CMIPC-M6C08_G16...nc  # Canal 08 (Upper-level WV)
│   ├── OR_ABI-L2-CMIPC-M6C10_G16...nc  # Canal 10 (Lower-level WV)
│   ├── OR_ABI-L2-CMIPC-M6C11_G16...nc  # Canal 11 (Cloud-top IR)
│   ├── OR_ABI-L2-CMIPC-M6C12_G16...nc  # Canal 12 (Ozone)
│   ├── OR_ABI-L2-CMIPC-M6C13_G16...nc  # Canal 13 (Clean IR)
│   ├── OR_ABI-L2-CMIPC-M6C14_G16...nc  # Canal 14 (IR Longwave)
│   └── OR_ABI-L2-CMIPC-M6C15_G16...nc  # Canal 15 (Dirty IR)
│
├── 📂 reproduction/               # Scripts de demo y reproducibilidad
│   ├── run_demo.sh                # Demo completo (4 tests: truecolor, ash, composite)
│   ├── crea_rgbs.sh               # Generación batch de productos RGB
│   ├── download_sample.sh         # Descarga de datos de ejemplo
│   └── expected_output/           # Salidas de referencia para validación
│
├── 🔧 Makefile                    # Sistema de construcción (gcc + GDAL + NetCDF)
├── 📖 README.md                   # Documentación principal
├── 📝 LICENSE                     # Licencia GPLv3
├── 📝 TODO.txt                    # Tareas pendientes
├── 📝 codemeta.json               # Metadatos de software (schema.org)
│
├── 📊 plan_rayleigh.md            # Documentación de corrección Rayleigh
├── 📊 PLAN_GEOTIFF.md             # Documentación de GeoTIFF
├── 📊 PLAN_FIX_CLIP_CORNERS.md    # Optimización de clipping
├── 📊 implementacion_clahe.md     # Detalles de implementación CLAHE
│
└── 🧪 Scripts auxiliares
    ├── extract_rayleigh_lut.py    # Extracción de LUTs desde pyspectral
    ├── compara_gdal.sh            # Comparación con GDAL
    ├── valida_geotiff.py          # Validación de GeoTIFF
    └── test_clip_fix.sh           # Testing de clipping
```

**Organización modular**:
- **`include/`**: Headers públicos con prototipos y documentación de API
- **`src/`**: Implementaciones en C11 con optimizaciones OpenMP
- **`sample_data/`**: Datos GOES-16 L2 CMI del 2024-08-07 18:01 UTC (10 canales)
- **`reproduction/`**: Scripts para demos y validación de reproducibilidad

**Archivos de datos embebidos**:
- Las LUTs de Rayleigh están compiladas en el ejecutable (no se requieren archivos .bin externos)
- Recortes geográficos predefinidos cargados desde `clips.csv` en memoria

---

## 🎯 Ventajas Científicas y Técnicas

### Comparación con GDAL y geo2grid

HPSATVIEWS ofrece ventajas significativas para procesamiento operacional y científico de datos GOES:

#### 1. **Velocidad de Procesamiento (30-120× más rápido)**

**Benchmark típico - Generación de True Color RGB (5424×5424 píxeles):**
- **HPSATVIEWS**: 0.5-1.0 segundos (C11 optimizado + OpenMP)
- **geo2grid**: 30-60 segundos (Python + NumPy)
- **GDAL**: 45-120 segundos (múltiples llamadas CLI)

**Razones de la diferencia:**
- Código nativo C11 compilado vs interpretado Python
- Paralelización OpenMP en operaciones críticas (downsampling, interpolación, CLAHE)
- Operaciones atómicas sin overhead de locks
- Gestión eficiente de memoria (sin garbage collector)
- Pipeline integrado (sin I/O intermedio entre etapas)

**Aplicaciones prácticas:**
- Procesamiento en tiempo casi real (alertas meteorológicas)
- Generación masiva de productos históricos
- Sistemas embebidos o con recursos limitados

#### 2. **Algoritmos Mejorados**

**CLAHE (Contrast Limited Adaptive Histogram Equalization):**
- ✅ **HPSATVIEWS**: Implementación completa con interpolación bilinear, paralelizada
- ❌ **GDAL**: No disponible nativamente
- ⚠️ **geo2grid**: Disponible vía scikit-image (lento, sin optimización para imágenes satelitales)

**Verde Sintético (True Color):**
- ✅ **HPSATVIEWS**: Coeficientes EDC optimizados para GOES-R (0.45706946, 0.48358168, 0.06038137)
- ✅ **geo2grid**: Similar (Miller et al. 2012)
- ⚠️ **GDAL**: Requiere procesamiento manual con gdal_calc.py (lento y complejo)

**Corrección Rayleigh:**
- ✅ **HPSATVIEWS**: LUTs embebidas en ejecutable (sin I/O de disco), interpolación trilinear optimizada
- ✅ **geo2grid**: LUTs desde pyspectral (lectura de disco cada ejecución)
- ❌ **GDAL**: No disponible

**Recorte Geográfico Inteligente:**
- ✅ **HPSATVIEWS**: Estrategia PRE-clip + POST-clip con muestreo denso de bordes (84 puntos)
  - Recorta en espacio geoestacionario ANTES de reproyectar (evita procesar píxeles innecesarios)
  - Inferencia automática de esquinas fuera del disco visible
- ⚠️ **GDAL**: Reproyecta primero, recorta después (ineficiente)
- ⚠️ **geo2grid**: Similar a GDAL

#### 3. **Flexibilidad de Uso**

**Interfaz unificada:**
- ✅ **HPSATVIEWS**: Un solo ejecutable, tres comandos coherentes (`rgb`, `pseudocolor`, `singlegray`)
  - Opciones comunes estandarizadas (`--clip`, `--gamma`, `--histo`, `--clahe`, `-r`, etc.)
  - Detección automática de formato de salida (PNG/GeoTIFF) por extensión
- ❌ **GDAL**: 100+ utilidades CLI distintas (gdal_translate, gdalwarp, gdal_calc.py, etc.)
  - Requiere encadenar múltiples comandos para workflows complejos
  - Sintaxis inconsistente entre herramientas
- ⚠️ **geo2grid**: Scripts Python monolíticos con configuración YAML compleja

**Procesamiento incremental:**
- ✅ **HPSATVIEWS**: Aplica operaciones en memoria en orden lógico:
  1. Gamma → Histogram/CLAHE → Scale → Clip → Reproject
  2. Sin archivos intermedios
- ❌ **GDAL**: Requiere archivos temporales entre cada paso (alto overhead de I/O)

**Paletas de colores:**
- ✅ **HPSATVIEWS**: Formato CPT (Generic Mapping Tools) - estándar en meteorología
- ✅ **GDAL**: Soporta color tables, pero sintaxis menos intuitiva
- ⚠️ **geo2grid**: Paletas hardcodeadas en código Python

#### 4. **Reproducibilidad Científica**

**Compatibilidad con estándares:**
- ✅ Corrección Rayleigh compatible con geo2grid/satpy (LUTs de pyspectral)
- ✅ Verde sintético con coeficientes EDC publicados (Miller et al. 2012)
- ✅ GeoTIFF con metadatos WKT estándar OGC (compatible con QGIS, ArcGIS, GDAL)
- ✅ Proyección geoestacionaria (PROJ_GEOS) con parámetros exactos de GOES-R

**Trazabilidad:**
- Código abierto (GPL v3) con algoritmos documentados
- Logging estructurado para debugging y validación
- Sin dependencias opacas (solo bibliotecas estándar: NetCDF, PNG, GDAL)

#### 5. **Eficiencia de Recursos**

**Memoria:**
- Gestión explícita con constructores/destructores (sin memory leaks)
- Sin overhead de runtime (GC, intérprete)
- Procesamiento in-place cuando es posible

**Portabilidad:**
- Ejecutable standalone (LUTs embebidas, sin archivos auxiliares)
- Compilación estática posible para distribución sin dependencias
- Compatible con Linux, macOS, Windows (MSYS2)

**Escalabilidad:**
- OpenMP para usar todos los cores disponibles
- Thread-safe sin locks (operaciones atómicas)
- Lineal en tamaño de imagen (O(N) para mayoría de operaciones)

### Casos de Uso Ideales

| **Escenario** | **Herramienta Recomendada** | **Razón** |
|---------------|----------------------------|----------|
| Procesamiento operacional en tiempo real | **HPSATVIEWS** | Velocidad crítica |
| Generación masiva de productos (años de datos) | **HPSATVIEWS** | 100× más rápido ahorra días de CPU |
| Mejora de contraste en imágenes con variación local | **HPSATVIEWS** | CLAHE optimizado |
| True color con corrección atmosférica | **HPSATVIEWS** o geo2grid | Ambos siguen estándares |
| Reproyecciones complejas (no lat/lon) | **GDAL** | Mayor variedad de proyecciones |
| Análisis geoespacial complejo | **GDAL** | Ecosistema completo |
| Workflows automatizados con configuración YAML | **geo2grid** | Diseñado para batch processing |

### Referencias para Publicación

**Algoritmos implementados:**
- Miller, S. D., et al. (2012). "A sight for sore eyes: The return of true color to geostationary satellites." *Bulletin of the American Meteorological Society*, 93(10), 1803-1816.
- Pizer, S. M., et al. (1987). "Adaptive histogram equalization and its variations." *Computer Vision, Graphics, and Image Processing*, 39(3), 355-368.
- Bodhaine, B. A., et al. (1999). "On Rayleigh optical depth calculations." *Journal of Atmospheric and Oceanic Technology*, 16(11), 1854-1861.

**Software comparado:**
- GDAL: Geospatial Data Abstraction Library. https://gdal.org
- geo2grid: NOAA/SSEC polar2grid + geostationary support. https://www.ssec.wisc.edu/software/geo2grid/
- satpy: Python package for satellite data processing. https://satpy.readthedocs.io

---

## 🔍 Datos de Entrada

### Formato Soportado
- **GOES-16/18/19 Level 1b NetCDF** (Radiance data) - Operacionales
- **GOES-17 Level 1b NetCDF** - Datos históricos (satélite retirado)
- **GOES-16/18/19 Level 2 NetCDF** (CMI - Cloud and Moisture Imagery)
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
make libhpsatviews.a   # Solo la biblioteca estática
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
2. **Resampling automático** de canales a resolución común:
   - Detecta resolución más alta entre todos los canales
   - Upsamplea canales de menor resolución usando interpolación bilineal
   - Ejemplo: C01 (1km) + C02 (500m) + C03 (1km) → todos a 500m
   - Downsampling ya NO se aplica por defecto (preserva máxima calidad)
3. **Canal verde sintético** usando coeficientes EDC: `0.45706946*C01 + 0.48358168*C02 + 0.06038137*C03`
4. **Normalización radiométrica** con factores de escala NetCDF
5. **Corrección gamma** para visualización óptima (recomendado: 2.0)
6. **Mejora de contraste** (opcional):
   - **Ecualización global** (`--histo`): Histograma acumulativo sobre toda la imagen
   - **CLAHE** (`--clahe`): Ecualización adaptativa por tiles con control de contraste

### CLAHE (Contrast Limited Adaptive Histogram Equalization)
1. **División en tiles**: Imagen dividida en grilla (defecto: 8×8)
2. **Histograma por tile**: Cálculo de histograma local (256 bins para 8-bit)
3. **Clipping de histograma**: Limita amplificación de contraste según `clip_limit`:
   - `clip_limit = (1.0 + clip_factor) × pixels_per_tile / 256`
   - Defecto: `clip_factor = 4.0` (optimizado para imágenes satelitales)
   - Valores típicos: 2.0-3.0 (fotografía), 4.0-6.0 (satélite)
4. **Redistribución uniforme**: Píxeles excedentes redistribuidos uniformemente en otros bins
5. **Mapeo CDF**: Función de distribución acumulativa para ecualización
6. **Interpolación bilinear**: Entre 4 tiles vecinos para evitar artefactos de bloques
7. **Procesamiento por canal**: Aplica a RGB independientemente, preserva canal alfa
8. **Paralelización OpenMP**: Cálculo de LUTs y aplicación de píxeles en paralelo

**Ventajas vs ecualización global:**
- ✅ Preserva detalles en regiones oscuras y brillantes simultáneamente
- ✅ Evita sobre-amplificación de ruido (control vía `clip_limit`)
- ✅ Ideal para imágenes con variaciones locales de iluminación (nubes, sombras)
- ⚠️ Puede introducir artefactos en imágenes uniformes (usar `--histo` en su lugar)

**Parámetros recomendados:**
- **Imágenes GOES completas**: `--clahe "8,8,4.0"` (defecto)
- **Recortes regionales pequeños**: `--clahe "4,4,3.0"` (menos tiles para áreas pequeñas)
- **Detección de estructuras finas**: `--clahe "16,16,5.0"` (más tiles, más contraste local)

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

### Gestión de Conjuntos de Canales (ChannelSet)
```c
// Crear conjunto de canales con resampling automático
ChannelSet* chset = channelset_create(3);  // 3 canales (R, G, B)

// Agregar canales con diferentes resoluciones
channelset_add(chset, &channel_r);  // 500m
channelset_add(chset, &channel_g);  // 1km
channelset_add(chset, &channel_b);  // 1km

// Upsamplear automáticamente todos los canales a la resolución más alta
channelset_upsample_all(chset);  // Todos a 500m

// Acceder a canales resampleados
DataF* r = channelset_get(chset, 0);
DataF* g = channelset_get(chset, 1);
DataF* b = channelset_get(chset, 2);

// Liberar
channelset_destroy(&chset);
```

### Procesamiento de Canales
```c
// Cargar datos NetCDF (funciona con L1b y L2)
DataNC channel;
load_nc_sf("archivo.nc", "Rad", &channel);  // L1b
load_nc_sf("archivo.nc", "CMI", &channel);  // L2

// Resampling automático inteligente
// upsample_bilinear() ahora acepta dimensiones objetivo
DataF resampled = upsample_bilinear(&source, target_width, target_height);

// Remuestreo manual con factor específico
DataF downsampled = downsample_boxfilter(channel.base, factor);

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

// 5. Aplicar corrección atmosférica de Rayleigh
// Las LUTs están embebidas en el ejecutable, se cargan automáticamente
RayleighLUT lut = rayleigh_lut_load("rayleigh_lut_C01.bin");  // Detecta C01/C02/C03 automáticamente
apply_rayleigh_correction(&reflectance_image, &sza, &vza, &raa, &lut);
rayleigh_lut_destroy(&lut);

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

## 📅 Historial de Cambios

### Diciembre 2025 - Patrones de Archivo, Resampling Automático y Refactorización

**Nuevos patrones de expansión de archivo:**
- ✅ Patrón `{CH}`: Extrae número de canal/banda del nombre de archivo (C01, C02, C13, etc.)
- ✅ Patrón `{SAT}`: Extrae nombre del satélite (_G16 → goes-16, _G18 → goes-18, _G19 → goes-19)
- ✅ Funciona en todos los comandos (rgb, singlegray, pseudocolor)
- ✅ Ejemplos: `"test_{SAT}_{CH}_{YYYY}{MM}{DD}.png"` → `test_goes-16_C01_20240807.png`

**Resampling automático inteligente:**
- ✅ Detecta automáticamente resolución más alta entre canales de entrada
- ✅ Upsamplea canales de menor resolución usando interpolación bilineal de alta calidad
- ✅ Preserva máxima calidad: C01 (1km) + C02 (500m) + C03 (1km) → todos a 500m
- ✅ Elimina downsampling por defecto (antes forzaba todo a 2km)
- ✅ Implementación optimizada: `upsample_bilinear()` ahora acepta dimensiones objetivo

**Refactorización mayor de código RGB:**
- ✅ Nuevo módulo `channelset`: Gestión unificada de conjuntos de canales con diferentes resoluciones
- ✅ Movido `rgb.h` de `src/` a `include/` para mejor organización
- ✅ Creados headers faltantes: `singlegray.h`, `nocturnal_pseudocolor.h`, `daynight_mask.h`
- ✅ Nueva función `create_multiband_rgb()` en `truecolor_rgb.c` para composiciones multi-banda
- ✅ Eliminado código duplicado y mejorada mantenibilidad

**Reorganización de estructura del proyecto:**
- ✅ Separación completa de headers (`include/`) y código fuente (`src/`)
- ✅ Todos los archivos `.h` movidos a `include/` para API clara y consistente
- ✅ Todos los archivos `.c` movidos a `src/` para mejor organización
- ✅ Carpeta `sample_data/` consolidada con 10 canales GOES-16 L2 CMI de ejemplo
- ✅ Carpeta `reproduction/` para scripts de demo y validación

**Mejoras en run_demo.sh:**
- ✅ Actualizado para usar archivos de `sample_data/`
- ✅ Agregada prueba de composite con todas las opciones avanzadas
- ✅ Incluye: `--geographics`, `--clip mexico`, `--rayleigh`, `--citylights`, `--alpha`, `--histo`, `--gamma 1.2`, `--scale -2`, `--geotiff`
- ✅ 4 tests completos: truecolor, ash, composite con full-disk y recorte

### Diciembre 2025 - CLAHE, Estandarización de CLI y Optimización de Clipping

**Implementación de CLAHE (Contrast Limited Adaptive Histogram Equalization):**
- ✅ Nueva opción `--clahe [params]` común a todos los comandos
  - Parámetros: `"tiles_x,tiles_y,clip_limit"` (defecto: `"8,8,4.0"`)
  - Ejemplo: `--clahe "16,16,5.0"` para más detalle local
- ✅ Implementación completa en `image.c`:
  - `clip_histogram()` con redistribución uniforme de píxeles excedentes
  - `calculate_cdf_mapping()` para ecualización por tile
  - `image_apply_clahe()` con interpolación bilinear entre tiles
  - Paralelización OpenMP en cálculo de LUTs y aplicación de píxeles
- ✅ Integración en `processing.c` (singlegray, pseudocolor) y `rgb.c`
  - Se aplica después de gamma/histogram, antes de scale
  - Soporta canal alpha (procesa solo RGB, ignora alpha)
  - Modo composite: aplica a diurna antes del blend
  - Otros modos RGB: aplica a final_image
- ✅ Default `clip_limit=4.0` optimizado para imágenes satelitales GOES
- ✅ Mejora notable en contraste local vs ecualización global
- ✅ Compatible con `--histo` (se pueden usar simultáneamente)

**Estandarización de interfaz de línea de comandos:**
- ✅ Unificadas opciones `--histo`, `--scale`, `--alpha` en los tres comandos
- ✅ Comando `rgb` ahora soporta `--histo` como opción (antes hardcodeado en truecolor/composite)
- ✅ Eliminada opción `--invert` de `pseudocolor` (sin sentido con paletas de colores)
- ✅ Opción `--invert` permanece exclusiva de `singlegray`
- ⏳ Opciones `--scale` y `--alpha` registradas en `rgb` pero pendientes de implementación

**Mejoras en clipping y reproyección:**
- ✅ Estrategia de recorte optimizada: PRE-clip en espacio geoestacionario antes de reproyectar
- ✅ Muestreo denso de bordes (84 puntos) para cálculo preciso de bounding box
- ✅ Función compartida `reprojection_find_bounding_box()` elimina ~200 líneas de código duplicado
- ✅ POST-clip fino para ajuste exacto al dominio solicitado
- ✅ Inferencia inteligente de esquinas cuando el dominio se extiende fuera del disco visible

**Cálculo de resolución mejorado:**
- ✅ Lectura de `spatial_resolution` desde metadatos NetCDF (campo `native_resolution_km`)
- ✅ Fórmula WGS84 para conversión km/grado dependiente de latitud: `111.132954 - 0.559822×cos(2×lat)`
- ✅ Resolución cuadrática (igual para lon/lat) para compatibilidad con GDAL
- ✅ Precisión de dimensiones: <1% de diferencia vs GDAL sin reproyección, ~10% con reproyección

**Resultados de validación:**
- Sin reproyección: 1316×805 vs 1317×808 GDAL (diferencia <0.3%)
- Con reproyección: 1482×861 vs 1352×785 GDAL (diferencia ~10%, aceptable)
- Coherencia geográfica confirmada con fronteras de mapdrawer

---

## 🙏 Agradecimientos

- NOAA por los datos GOES-16/18/19 y datos históricos de GOES-17
- Comunidad NetCDF por las bibliotecas de acceso a datos
- Desarrolladores de libpng por el procesamiento de imágenes
- Proyecto GDAL por las bibliotecas de georreferenciación

---

*HPSATVIEWS - Procesamiento satelital de alta velocidad para meteorología operacional*
