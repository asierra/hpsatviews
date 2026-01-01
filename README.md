# High Performance Satellite Views (HPSATVIEWS)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C11](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#building)

**HPSATVIEWS - Visualización de datos satelitales de alto rendimiento**

## 1. Introducción

### 1.1 Resumen

**HPSATVIEWS** es un sistema de generación de **vistas 
y productos visuales** de alto rendimiento a partir de datos satelitales 
ambientales. Permite generar vistas en escala de grises, pseudocolor y 
compuestos RGB en tiempos del orden de segundos, manteniendo rigor geométrico 
y reproducibilidad. Está optimizado para satélites geoestacionarios de la 
familia **GOES-R**. 

### 1.2 Filosofía de diseño

Está diseñado exclusivamente para operar en el dominio de las 
**vistas** y **productos visuales**. No sustituye plataformas de análisis 
físico ni herramientas GIS generalistas. Su objetivo es ofrecer un flujo 
de trabajo simple, muy rápido y conceptualmente claro para la interpretación 
visual de escenas satelitales.

---

## 2. Conceptos fundamentales

Conceptos y términos usados en el contexto de este proyecto.

### Imagen

Representación numérica de una escena física continua, organizada 
como una colección de bandas que registran la distribución espacial y espectral 
de magnitudes físicas —como radiancia, temperatura o reflectancia— mediante 
elementos lógicos discretos. [Lira, 2010]

### Vista (view)

Representación derivada de una imagen, normalizada y cuantizada para 
su interpretación por el sistema visual humano.

### Producto

Vista asociada a un concepto reconocible por la comunidad de ciencias 
ambientales (por ejemplo: *true color*, *air mass*, *ash*).

### Instante (timestamp)

Se denomina **instante** al momento temporal asociado a una escena satelital, 
definido por la hora efectiva de observación del sensor y representado mediante 
un conjunto discreto de componentes temporales (año, día juliano, hora, 
minuto, segundo).

---

## 3. Uso básico

*High Performance Satellite Views* se utiliza desde la línea de comandos con una sintaxis simple:

```bash
hpsv <comando> <archivo_ancla> [opciones]
```

El **archivo ancla** en formato NetCDF permite identificar la escena, su instante y su ruta. El sistema infiere automáticamente los archivos de las bandas necesarias.

### Ejemplo

```bash
hpsv gray OR_ABI-L1b-RadF-M6C13_G16.nc
```

Genera una vista en escala de grises del canal C13.

---

## 4. Uso avanzado

### 4.1 Comandos disponibles

* `gray` – Vista en escala de grises de un canal individual o una combinación lineal de canales.
* `pseudocolor` – Vista con mapa de colores de un canal individual o una combinación lineal de canales.
* `rgb` – Composición RGB a partir de tres combinaciones lineales de múltiples canales.

### 4.2 Opciones globales

* `--help`

  Muestra la ayuda general. En la siguiente sección damos más detalles.

* `--list-clips` – Lista recortes geográficos predefinidos en un 
  archivo CSV con las columnas *clave, nombre, ul_x, ul_y, lr_x, lr_y*. 
  Ejemplo:
```csv    
  mexico,Mexico,-121.3325136900594,32.9450945620932,-83.9198061602870,9.8346808199271
  caribe,Caribe,-93.0476928458730,28.0613844882756,-56.01289145276628,5.12538896303195
```

### 4.3 Opciones comunes

* `-a, --alpha`
  Añade un canal alfa para transparencia en regiones sin datos o fuera de un umbral específico.

* `-c, --clip <valor>`
  Recorta la imagen. El valor puede ser:

  * una clave predefinida (por ejemplo, `mexico`), o
  * coordenadas explícitas, en grados decimales, longitud oeste negativa, entre comillas o separadas por comas:
    `"lon_min lat_max lon_max lat_min"`

  Ejemplos:
  ```bash
  # Usar un recorte predefinido con clave
  hpsv gray -c mexico -o recorte.png archivo.nc
  
  # Con comas (sin comillas ni espacios)
  hpsv rgb -m ash -c -107.23,22.72,-93.84,14.94 -o recorte.png archivo.nc

  # Con espacios (CON comillas)
  hpsv rgb -m ash -c "-107.23 22.72 -93.84 14.94" -o recorte.png archivo.nc
  ```

* `--clahe`
  Aplica ecualización adaptativa de histograma (CLAHE) con parámetros predefinidos (`8,8,4.0`).

* `--clahe-params <params>`
  Misma opción CLAHE pero permite especificar parámetros en el formato:
  `tiles_x,tiles_y,clip_limit`

  Ejemplo:

  ```bash
  --clahe-params "16,16,5.0"
  ```
 
* `-g, --gamma <valor>`
  Aplica corrección gamma (por omisión `1.0` y no se aplica).


* `-h, --histo`
  Aplica ecualización de histograma global. Si genera zonas saturadas de contraste, usar mejor CLAHE.
  

* `-o, --out <archivo>`
  Archivo de salida. Si no se especifica, el nombre se genera automáticamente.
  Soporta patrones con marcadores entre llaves:

  * `{YYYY}` año
  * `{YY}` año (2 dígitos)
  * `{MM}` mes
  * `{DD}` día
  * `{hh}` hora
  * `{mm}` minuto
  * `{ss}` segundo
  * `{JJJ}` día juliano
  * `{TS}` Instante (timestamp) YYYYJJJhhmm
  * `{CH}` canal o banda (C01, C02, etc.)
  * `{SAT}` satélite (por ejemplo: `G16`, `G19`)

  Ejemplo:

  ```bash
  hpsv gray -o "ir_{SAT}_{CH}_{YYYY}{MM}{DD}.png" \
        OR_ABI-L1b-RadF-M6C13_G19_s20253551801183.nc
  # → ir_G19_C15_20251221.png
  ``` 

* `-r, --geographics`
  Reproyecta la salida a coordenadas geográficas (latitud/longitud) equirrectangulares.

* `-s, --scale <factor>`
  Factor entero de escala espacial. Valores mayores que 1 amplían la 
  imagen; valores menores que 1 la reducen (por omisión `1` y no se 
  aplica). Un valor -2 implica una escala de 0.5. Obligatorio **usar 
  solo enteros**.

* `-t, --geotiff`
  Genera la salida en formato **GeoTIFF** georreferenciado, con 
  metadatos de proyección completos, compatible con QGIS, GDAL, ArcGIS, 
  etc.

  Ejemplos:
  ```bash
	# Opción explícita
	hpsv gray -t archivo.nc

	# Detección automática por extensión
	hpsv gray -o salida.tif archivo.nc
  ```

* `-v, --verbose`
  Activa el modo verboso, mostrando información detallada del procesamiento.

### 4.4 Opciones comando *gray*

Genera una vista en escala de grises.

* `-i, --invert`
  Invierte los valores (blanco a negro).

### 4.5 Opciones comando *pseudocolor*

Asocia un mapa de color a una vista en grises.

* `-p, --cpt <archivo>`     Aplica una paleta de colores (archivo .cpt) (omisión: arcoiris predefinido).
* `-i, --invert`            Invierte los valores (mínimo a máximo).
  
  Ejemplo:
  ```bash
  hpsv pseudocolor -p paleta.cpt archivo_GOES.nc
  ```

### 4.6 Opciones comando *rgb*

Genera un compuesto RGB a partir de combinaciones lineales de varias bandas.

* `-m, --mode <modo>`       Modo de operación. Opciones disponibles: 
							`daynite` (predeterminado), `truecolor`, `night`, `ash`, `airmass`, `so2`, `custom`. 

* `--rayleigh`              Aplica corrección atmosférica de Rayleigh (solo modos visibles diurnos).
							Por defecto usa LUTs de pyspectral (más precisas).

* `--ray-analytic`          Usa corrección Rayleigh analítica en lugar de LUTs (más ligera, menos precisa).

* `-f, --full-res`          Usa el canal de mayor resolución como referencia (más detalle, más lento).
							Por omisión, se usa el de menor resolución (más rápido, vistas menos grandes).


  Ejemplos:
  ```bash
  # Compuesto día/noche predeterminado, con Rayleigh y realce de contraste implícitos
  hpsv rgb -o dianoche.png archivo.nc
  
  # True color con corrección atmosférica de Rayleigh y CLAHE
  hpsv rgb -m truecolor --rayleigh --clahe archivo.nc

  # Detección de ceniza volcánica
  hpsv rgb -m ash -o ceniza.png archivo.nc
  ```
  
El modo `daynite` hace una mezcla inteligente de los modos `truecolor` 
y `night` con luces de ciudad de fondo, usando una máscara precisa con 
base en la geometría solar, y aplica automáticamente corrección 
Rayleigh y realce de contraste.

Para modo `custom` ver **Álgebra de bandas**.

### 4.7 Convenciones de salida

Si no se especifica la opción `-o` o `--out`, se genera un nombre determinista basado en los metadatos del archivo "ancla", las bandas y las operaciones aplicadas:

**Formato:** `hpsv_<SAT>_<YYYYJJJ>_<hhmm>_<COMMAND>_<CH>[_<OPS>].<ext>`

Ejemplo:
  ```bash
  hpsv gray OR_ABI-L1b-RadF-M6C13_G16_s20242190300217.nc
  # → hpsv_G16_2024219_0300_gray_C13.png
  ```
  
### 4.8 Álgebra de bandas y composiciones personalizadas

`hpsv` permite definir combinaciones lineales de bandas al vuelo para generar composiciones RGB o imágenes monocanal complejas sin necesidad de generar archivos intermedios.

**Sintaxis Soportada:**
* **Términos con coeficientes por banda:** (ej. `2.0*C13`).
* **Operadores:** `+`, `-` entre los términos.
* **Rangos:** Opcionalmente, mínimos y máximos separados por comas. Por omisión se calculan.
* **Separadores:** Usa punto y coma `;` para separar las componentes R, G y B (solo con comando `rgb`).

#### Ejemplos de Uso

**1. Álgebra Monocanal** en los comandos gray o pseudocolor.

```bash
hpsv gray archivo_ancla.nc \
  --expr "C13-C15" \
  --minmax "0.0,100.0"
```

**2. Composición RGB Personalizada** Define fórmulas independientes para los canales Rojo, Verde y Azul usando el modo `custom`. Nota el uso de comillas para proteger los espacios y el punto y coma.

```bash
hpsv rgb archivo_ancla.nc \
  --mode custom \
  --expr "C13-C14; C13-C11; C13" \
  --minmax "-2,2; -4,2; 240,300" \
  --out "ceniza_volcanica.png"
```

---

## 5. Detalles técnicos

### 5.1 Geometría y geolocalización

La generación de vistas se apoya en formulaciones geométricas rigurosas. El sistema implementa reproyección directa desde proyección geoestacionaria a malla lat/lon uniforme (WGS84), con manejo de huecos e inferencia automática de dominios fuera del disco visible. El recorte geográfico se optimiza cuando es posible, realizándolo antes de la reproyección.

### 5.2 Corrección atmosférica (Rayleigh)

HPSATVIEWS incorpora corrección de dispersión de Rayleigh para productos visibles, siguiendo formulaciones estándar reportadas en la literatura, mejorando la fidelidad visual de escenas diurnas.

### 5.3 CLAHE

El sistema incluye ecualización adaptativa de histograma con control de contraste local (CLAHE) para mejorar la interpretabilidad visual en escenas con variaciones espaciales pronunciadas de contraste.

### 5.4 Rendimiento

Implementado en C11 (ISO/IEC 9899:2011) con paralelización mediante OpenMP, HPSATVIEWS prioriza el alto rendimiento, el uso eficiente de memoria y la escalabilidad en sistemas multi-núcleo.

---

## 6. Requisitos

* Compilador C compatible con C11
* Bibliotecas:
  - **libnetcdf-dev** - Lectura de archivos NetCDF GOES L1b
  - **libpng-dev** - Generación de imágenes PNG
  - **libgdal-dev** - Generación de imágenes GeoTIFF
  - **libm** - Funciones matemáticas
  - **OpenMP** - Paralelismo.

---

## 7. Estado del proyecto

HPSATVIEWS se encuentra en desarrollo activo, funcional estable y ampliación progresiva de capacidades y documentación.

---

## 8. Referencias
- Bah, K., Schmit, T. J., Gerth, J., Cronce, M., otkin, J., & Li, J. (2018).
  GOES-16 Advanced Baseline Imager (ABI) True Color Imagery for Legacy and 
  Non-Traditional Applications. NOAA/CIMSS.
- Bodhaine, B. A., et al. (1999). "On Rayleigh optical depth 
  calculations." *Journal of Atmospheric and Oceanic Technology*, 16(11), 
  1854-1861.
- Bucholtz, A. (1995). Rayleigh-scattering calculations for the terrestrial 
  atmosphere. Applied Optics, 34(15), 2765-2773.
- Hansen, J. E., & Travis, L. D. (1974). Light scattering in planetary 
  atmospheres. Space Science Reviews, 16(4), 527-610.
- Lira Chávez, J. (2010). Tratamiento digital de imágenes 
  multiespectrales (2a ed.). México, D. F.: Instituto de Geofísica, 
  Universidad Nacional Autónoma de México
- Miller, S. D., et al. (2012). "A sight for sore eyes: The return of 
  true color to geostationary satellites." *Bulletin of the American 
  Meteorological Society*, 93(10), 1803-1816.
- Pizer, S. M., et al. (1987). "Adaptive histogram equalization and its 
  variations." *Computer Vision, Graphics, and Image Processing*, 39(3), 
  355-368.
- PySpectral Atmospheric correction Look Up Tables. Available online: 
  https://doi.org/10.5281/zenodo.1205534 (accessed on 2 October 2025) 
- Scheirer, Ronald & Dybbroe, Adam & Raspaud, Martin. (2018). A General 
  Approach to Enhance Short Wave Satellite Imagery by Removing Background 
  Atmospheric Effects. Remote Sensing. 10. 10.3390/rs10040560.   
- Zuiderveld, K. (1994). Contrast Limited Adaptive Histogram 
  Equalization. In P. S. Heckbert (Ed.), Graphics Gems IV (pp. 474–485). 
  Academic Press.
  

## 9. Autor y licencia

```
Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
Laboratorio Nacional de Observación de la Tierra, UNAM

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

Consulta el archivo [LICENSE](LICENSE) para más detalles.

---

*HPSATVIEWS - Visualización de datos satelitales de alto rendimiento*
