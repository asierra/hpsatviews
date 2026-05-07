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
  En modo RGB acepta 3 valores separados por `;` para aplicar un gamma distinto
  a cada canal (R;G;B):
  ```
  hpsv rgb -g "1.8;1.5;1.2" archivo.nc
  ```
  Con un solo valor se aplica igual a los 3 canales.


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
  * `{SECTOR}` sector de escaneo: `fd`, `conus`, `m1` o `m2`
  * `{PROD}` nombre corto del modo (ej. `truecolor`, `ash`); reemplazado por `--name` si se usa

  Ejemplo:

  ```bash
  hpsv gray -o "ir_{SAT}_{SECTOR}_{CH}_{YYYY}{MM}{DD}.png" \\
        OR_ABI-L1b-RadC-M6C13_G19_s20253551801183.nc
  # → ir_G19_conus_C13_20251221.png
  ``` 

* `-G, --geographics`
  Reproyecta la salida a coordenadas geográficas (latitud/longitud) equirrectangulares.

* `-s, --scale <factor>`
  Factor entero de escala espacial. Valores mayores que 1 amplían la 
  imagen; valores menores que 1 la reducen (por omisión `1` y no se 
  aplica). Un valor -2 implica una escala de 0.5. Obligatorio **usar 
  solo enteros**.

* `-t, --geotiff`
  Genera la salida en formato **Cloud Optimized GeoTIFF (COG)** 
  georreferenciado, con tiling interno, overviews y metadatos de 
  proyección completos. Compatible con QGIS, GDAL, ArcGIS, y servicios 
  cloud como STAC, Titiler y cualquier cliente HTTP con range requests.

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

* `--minmax "<min>,<max>"`
  Fija el rango físico de valores que se mapean al rango 0–255, sin importar
  el mínimo/máximo real de los datos. Útil para comparar imágenes a distintas
  horas o escenas con distinto rango dinámico.

  Ejemplo: imágenes IR nocturnas comparables entre sí fijando temperatura
  en Kelvin:
  ```bash
  hpsv gray -i -s -4 archivo_G19_C13.nc -o ir_0600.png --minmax "193.15,313.15"
  hpsv gray -i -s -4 archivo_G19_C13_1200.nc -o ir_1200.png --minmax "193.15,313.15"
  ```
  Sin esta opción, cada imagen escala de forma independiente a su propio
  mínimo y máximo, impidiendo comparaciones visuales directas.

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
							`daynite` (predeterminado), `truecolor`, `night`, `ash`, `airmass`, `severestorm`, `so2`, `custom`. 

* `--rayleigh`              Aplica corrección atmosférica de Rayleigh (solo modos visibles diurnos).
							Por defecto usa LUTs de pyspectral (más precisas).

* `--ray-analytic`          Usa corrección Rayleigh analítica en lugar de LUTs (más ligera, menos precisa).

* `-f, --full-res`          Usa el canal de mayor resolución como referencia (más detalle, más lento).
							Por omisión, se usa el de menor resolución (más rápido, vistas menos grandes).

* `--stretch`               Aplica un estiramiento de contraste por tramos (*piecewise stretch*) similar al
							usado por geo2grid/Beaufort. Mejora la diferenciación tonal en escenas con rango
							dinámico comprimido (útil especialmente con `truecolor`).

* `--sharpen`               Aplica *ratio sharpening* para mejorar la nitidez espacial de las componentes
							verde y azul. Calcula por cada píxel la razón entre su valor y la media de su
							bloque 2×2 en el canal rojo (C02), y multiplica dicha razón en el verde y azul.
							Equivalente al `SelfSharpenedRGB` de satpy/geo2grid.
							El efecto es apreciable cuando se trabaja a resolución completa (`--full-res`)
							o con recortes geográficos (`--clip`). En disco completo a resolución reducida
							el beneficio es imperceptible.

* `-N, --name <etiqueta>`   Nombre descriptivo del producto. Se escribe en los metadatos JSON y GeoTIFF
						como campo `product` al nivel raíz (junto a `satellite`, `sector`, `timestamp`).
						También disponible como token `{PROD}` en los patrones de `-o`.
						Si se omite, `{PROD}` usa el nombre corto del modo (ej. `truecolor`) y `product` en el JSON
						usa la descripción del modo (ej. `"True Color RGB (natural)"`).

						Acepta el formato `corto:Descripción larga` para fijar ambos valores independientemente:
						la parte antes de `:` va a `{PROD}` en el nombre de archivo, y la parte tras `:` al campo
						`product` en el JSON/GeoTIFF. Si no hay `:`, el valor se usa para ambos.

						Especialmente útil con `--mode custom` para identificar la composición.
  # True color con corrección atmosférica de Rayleigh y CLAHE
  hpsv rgb -m truecolor --rayleigh --clahe archivo.nc

  # True color con Rayleigh, estiramiento y ratio sharpening (mayor nitidez)
  hpsv rgb -m truecolor --rayleigh --stretch --sharpen archivo.nc

  # Detección de ceniza volcánica
  hpsv rgb -m ash -o ceniza.png archivo.nc

  # Composición personalizada con nombre descriptivo en metadatos y nombre de archivo
  hpsv rgb -m custom --expr "C13-C14; C13; -1.0*C15+300" \
        --name "Ceniza volcánica" -o "{PROD}_{SAT}_{YYYY}{MM}{DD}.png" archivo.nc
  # → Ceniza volcánica_G16_20250101.png
  ```
  
El modo `daynite` hace una mezcla inteligente de los modos `truecolor` 
y `night` con luces de ciudad de fondo, usando una máscara precisa con 
base en la geometría solar, y aplica automáticamente corrección 
Rayleigh y realce de contraste.

Para modo `custom` ver **Álgebra de bandas**.

### 4.7 Archivo JSON sidecar

`hpsv` genera automáticamente un archivo JSON con metadatos del procesamiento junto a cada imagen de salida. Este archivo contiene información radiométrica, geoespacial y de procesamiento útil para trazabilidad y análisis posterior.

**Convención de nombres:**
* Si la imagen es `salida.png`, el JSON será `salida.json`
* El JSON se genera automáticamente, sin necesidad de opciones adicionales

**Contenido del JSON:**

```json
{
  "tool": "hpsatviews",
  "version": "1.0",
  "command": "rgb",
  "satellite": "G16",
  "sector": "conus",
  "timestamp": "2024-08-07T18:01:17Z",
  "product": "True Color RGB (natural)",
  "crs": "GEOGCS[...]",
  "bounds": [-110.5, 30.0, -90.0, 15.0],
  "channels": ["C01", "C02", "C03"],
  "processing": {
    "gamma": "1.8;1.5;1.2",
    "clahe_applied": true,
    "rayleigh_corrected": true
  },
  "geometry": {
    "projection": "geographics",
    "bounds": [-110.5, 15.0, -90.0, 30.0]
  },
  "output": {
    "filename": "salida.png",
    "width": 2000,
    "height": 1500
  }
}
```

**Casos de uso:**
* **Reproducibilidad:** Documentación exacta de parámetros usados
* **Integración:** Automatización de flujos de visualización (ej. mapdrawer)
* **Trazabilidad:** Auditoría de procesamiento para publicaciones científicas

### 4.8 Convenciones de salida

Si no se especifica la opción `-o` o `--out`, se genera un nombre determinista basado en los metadatos del archivo "ancla", las bandas y las operaciones aplicadas:

**Formato:** `hpsv_<SAT>[_<SECTOR>]_<YYYYJJJ>_<hhmm>_<COMMAND>_<CH>[_<OPS>].<ext>`

Ejemplo:
  ```bash
  hpsv gray OR_ABI-L1b-RadC-M6C13_G16_s20242190300217.nc
  # → hpsv_G16_conus_2024219_0300_gray_C13.png
  # → hpsv_G16_conus_2024219_0300_gray_C13.json (metadatos)
  ```
  
### 4.9 Álgebra de bandas y composiciones personalizadas

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

HPSATVIEWS incorpora corrección de dispersión de Rayleigh para canales
visibles, mejorando la fidelidad visual de escenas diurnas al remover la
contribución de dispersión molecular de la atmósfera.

**Implementación LUT (predeterminada, `--rayleigh`).** Utiliza tablas de
búsqueda (*look-up tables*) pre-calculadas a partir de pyspectral
(Scheirer et al., 2018), indexadas por tres variables: secante del ángulo
zenital solar, secante del ángulo zenital del satélite y diferencia de
ángulos azimutales. Las LUTs se embeben en el binario en tiempo de
compilación para evitar dependencias externas. La convención de azimuth
sigue a pyspectral: la LUT se indexa con `180° − Δφ`, donde Δφ es la
diferencia de azimut sol–satélite.

**Implementación analítica (`--ray-analytic`).** Alternativa más ligera
que calcula la corrección en tiempo real con el modelo de Bucholtz (1995)
y la función de fase de Rayleigh de Hansen & Travis (1974). Útil cuando
no se requiere la máxima precisión o se busca reducir el tamaño del
binario.

**Relajación en zonas nubosas.** Ambas implementaciones incorporan
relajación de la corrección donde la reflectancia del canal rojo
(C02, 0.64 µm) supera 0.20, siguiendo el criterio de pyspectral.
La corrección se reduce linealmente hasta anularse cuando la
reflectancia alcanza 1.0, evitando sobre-corrección en nubes y
superficies altamente reflectivas.

### 5.3 CLAHE

El sistema incluye ecualización adaptativa de histograma con control de contraste local (CLAHE) para mejorar la interpretabilidad visual en escenas con variaciones espaciales pronunciadas de contraste.

### 5.4 Composición True Color

El modo `truecolor` genera una imagen de color natural a partir de tres
canales ABI: C01 (0.47 µm, azul), C02 (0.64 µm, rojo) y C03
(0.865 µm, infrarrojo cercano). Dado que ABI no posee un canal verde
nativo, se sintetiza mediante la combinación lineal:

$$G = 0.465 \cdot B + 0.465 \cdot R + 0.07 \cdot NIR$$

Estos coeficientes reproducen los utilizados por geo2grid/satpy (Bah
et al., 2018) y proporcionan un verde perceptualmente equilibrado.

**Piecewise stretch (`--stretch`).** La reflectancia corregida se mapea
a niveles digitales mediante un estiramiento por tramos que expande
selectivamente los tonos oscuros y comprime los claros, mejorando la
diferenciación tonal en escenas con rango dinámico comprimido. La curva
es equivalente a la utilizada por geo2grid.

### 5.5 Rendimiento

Implementado en C11 (ISO/IEC 9899:2011) con paralelización mediante OpenMP, HPSATVIEWS prioriza el alto rendimiento, el uso eficiente de memoria y la escalabilidad en sistemas multi-núcleo.

---

## 6. Requisitos

* Compilador C compatible con C11
* Bibliotecas:
  - **libnetcdf-dev** - Lectura de archivos NetCDF GOES L1b
  - **libpng-dev** - Generación de imágenes PNG
  - **libgdal-dev** - Generación de imágenes COG (Cloud Optimized GeoTIFF)
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
