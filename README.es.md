# High Performance Satellite Views (HPSATVIEWS)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C11](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11)
[![CI](https://github.com/asierra/hpsatviews/actions/workflows/ci.yml/badge.svg)](https://github.com/asierra/hpsatviews/actions/workflows/ci.yml)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.21092353.svg)](https://doi.org/10.5281/zenodo.21092353)

Idiomas: [English](README.md) | **Español**

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

## 3. Instalación

### 3.1 Obtener el código

```bash
git clone https://github.com/asierra/hpsatviews.git
cd hpsatviews
```

### 3.2 Dependencias

* Compilador C compatible con C11 (gcc recomendado, con soporte OpenMP)
* Bibliotecas:
  - **libnetcdf-dev** - Lectura de archivos NetCDF GOES L1b/L2
  - **libpng-dev** - Generación de imágenes PNG
  - **libgdal-dev** - Generación de imágenes COG (Cloud Optimized GeoTIFF)
  - **libwebp-dev** - Lectura de la imagen de fondo (luces nocturnas) en modos `night`/`daynite`
  - **libm** - Funciones matemáticas
  - **OpenMP** - Paralelismo

En Debian/Ubuntu:

```bash
sudo apt install build-essential libnetcdf-dev libpng-dev libgdal-dev libwebp-dev
```

### 3.3 Compilar e instalar

```bash
# Build de producción (HPC: -O3 -march=native)
make

# Build de depuración (binario bin/hpsv_debug)
make DEBUG=1

# Build con textos de ayuda en español
make HPSV_LANG=es

# Instalación a nivel de sistema (binario + página de manual)
sudo make install
```

### 3.4 Verificar

```bash
hpsv --version
hpsv --help
```

### 3.5 Pruebas

El proyecto incluye una suite de pruebas de regresión end-to-end que corre
`hpsv` sobre datos de muestra reales y compara el resultado contra salidas
de referencia (PNG/GeoTIFF) con un diff de píxeles tolerante.

```bash
# Descarga datos de muestra (GOES-16, sin credenciales)
reproduction/download_sample.sh

# Corre la suite completa (compila el proyecto si es necesario)
tests/run_all_tests.sh
```

Esta misma suite se ejecuta automáticamente en cada *push*/*pull request* a
`main` vía GitHub Actions (ver badge de CI al inicio del documento).

---

## 4. Uso básico

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

## 5. Uso avanzado

### 5.1 Comandos disponibles

* `gray` – Vista en escala de grises de un canal individual o una combinación lineal de canales.
* `pseudocolor` – Vista con mapa de colores de un canal individual o una combinación lineal de canales.
* `rgb` – Composición RGB a partir de tres combinaciones lineales de múltiples canales.

### 5.2 Opciones globales

* `--help`

  Muestra la ayuda general. En la siguiente sección damos más detalles.

* `--list-clips` – Lista recortes geográficos predefinidos en un 
  archivo CSV con las columnas *clave, nombre, ul_x, ul_y, lr_x, lr_y*. 
  Ejemplo:
```csv    
  mexico,Mexico,-121.3325136900594,32.9450945620932,-83.9198061602870,9.8346808199271
  caribe,Caribe,-93.0476928458730,28.0613844882756,-56.01289145276628,5.12538896303195
```

### 5.3 Opciones comunes

* `-a, --alpha`
  Añade un canal alfa para transparencia en regiones sin datos o fuera de un umbral específico.

* `-B, --both`
  Guarda en una sola ejecución tanto la salida en proyección nativa como
  la reproyectada (geográfica) — implica `-G`, no es necesario pasarlo
  por separado. El archivo reproyectado recibe el sufijo `_geo` insertado
  antes de la extensión (ej. `out.png` → `out_geo.png`).

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
 
* `-f, --full-res`
  Usa el canal de mayor resolución como referencia en lugar del de menor
  resolución (por omisión), al combinar canales de distinta resolución
  nativa — en cualquier modo `rgb`, o con `--expr` multicanal en
  `gray`/`pseudocolor`. Más detalle espacial, más lento, salida más grande.

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

### 5.4 Opciones comando *gray*

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

### 5.5 Opciones comando *pseudocolor*

Asocia un mapa de color a una vista en grises.

* `-p, --cpt <archivo>`     Aplica una paleta de colores (archivo .cpt) (omisión: arcoiris predefinido).
* `-i, --invert`            Invierte los valores (mínimo a máximo).
  
  Ejemplo:
  ```bash
  hpsv pseudocolor -p paleta.cpt archivo_GOES.nc
  ```

### 5.6 Opciones comando *rgb*

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

* `-T, --cloud-temp <K>`    Clasifica como noche los píxeles más fríos que esta temperatura de brillo
							(Kelvin), independientemente de la geometría solar (solo modo `daynite`).
							Útil para capturar nubes altas y frías que la máscara del terminador
							día/noche por sí sola seguiría clasificando como diurnas. `0` desactiva
							la opción (por omisión); valor típico: `230`.

* `-l, --citylights`        Usa un fondo de luces de ciudad detrás del lado nocturno de la
							composición, en modo `night` independiente (sin la opción, el fondo es
							liso por omisión). En modo `daynite` esto siempre está activo sin
							importar la opción — el lado nocturno de la mezcla día/noche siempre
							muestra luces de ciudad.

* `-N, --name <etiqueta>`   Nombre descriptivo del producto. Se escribe en los metadatos JSON y GeoTIFF
						como campo `product` al nivel raíz (junto a `satellite`, `sector`, `timestamp`).
						También disponible como token `{PROD}` en los patrones de `-o`.
						Si se omite, `{PROD}` usa el nombre corto del modo (ej. `truecolor`) y `product` en el JSON
						usa la descripción del modo (ej. `"True Color RGB (natural)"`). Acepta el formato `corto:Descripción larga` para fijar ambos valores independientemente:
						la parte antes de `:` va a `{PROD}` en el nombre de archivo, y la parte tras `:` al campo
						`product` en el JSON/GeoTIFF. Si no hay `:`, el valor se usa para ambos.

Especialmente útil con `--mode custom` para identificar la composición.

  Ejemplos:
						
  ```bash						
  # True color con corrección atmosférica de Rayleigh y CLAHE
  hpsv rgb -m truecolor --rayleigh --clahe archivo.nc

  # True color con Rayleigh, estiramiento y ratio sharpening (mayor nitidez)
  hpsv rgb -m truecolor --rayleigh --stretch --sharpen archivo.nc

  # Detección de ceniza volcánica
  hpsv rgb -m ash -o ceniza.png archivo.nc

  # Composición personalizada con nombre descriptivo en metadatos y nombre de archivo
  hpsv rgb -m custom --expr "C13-C14; C13; -1.0*C15+300" \
        --name "ash:Ceniza volcánica" -o "{PROD}_{SAT}_{YYYY}{MM}{DD}.png" archivo.nc
  # → ash_G16_20250101.png
  ```
  
El modo `daynite` hace una mezcla inteligente de los modos `truecolor` 
y `night` con luces de ciudad de fondo, usando una máscara precisa con 
base en la geometría solar, y aplica automáticamente corrección 
Rayleigh y realce de contraste.

Para modo `custom` ver **Álgebra de bandas**.

### 5.7 Archivo JSON sidecar

`hpsv` puede escribir un archivo JSON con metadatos del procesamiento junto a la imagen de salida, útil para trazabilidad y para integraciones como `mapdrawer`.

**Convención de nombres y activación:**
* El JSON sidecar es opcional: se genera solo si se pasa `-j`/`--json`.
* Si la imagen es `salida.png`, el JSON será `salida.json`.

**Contenido del JSON** (ejemplo real, `hpsv gray archivo_CMIP_C13.nc -j -G --clahe -g 1.3`):

```json
{
  "tool": "hpsatviews",
  "version": "1.0.0",
  "satellite": "G16",
  "sector": "conus",
  "timestamp": "2024-08-07T13:02:36Z",
  "product": "CMIP",
  "command": "gray",
  "crs": "EPSG:4326",
  "bounds": [-151.654, 14.571, -52.947, 56.640],
  "geometry": {
    "projection": "EPSG:4326",
    "bbox": [-151.654, 14.571, -52.947, 56.640]
  },
  "channels": [
    {
      "name": "C13",
      "quantity": "brightness_temperature",
      "min": 191.633,
      "max": 301.757,
      "unit": "K"
    }
  ],
  "enhancements": {
    "gamma": 1.3,
    "clahe": true,
    "geographics": true,
    "output_file": "salida.png",
    "output_width": 5476,
    "output_height": 2334
  }
}
```

* `crs` refleja la proyección real de la salida: `EPSG:4326` si se reproyectó con `-G`/`--both`, `goes16`/`goes17`/`goes18`/`goes19` (o `geostationary`) en la rejilla nativa del satélite, o el valor por omisión `geographics` cuando no se calculó geometría (PNG plano sin `--clip`, GeoTIFF ni reproyección). `bounds`/`geometry.bbox` solo aparecen cuando sí se calculó geometría, y son redundantes entre sí (mismo recuadro en dos formas).
* `product` solo aparece para productos L2 (CMIP, ACHA, ACHT, ACTP, CTP, LST, SST); los archivos L1b (radiancia) no lo incluyen porque no tienen una identidad de "producto" distinta del canal.
* `enhancements` agrega una clave por cada opción de procesamiento efectivamente aplicada (entre otras: `gamma`, `clahe`, `histogram`, `invert`, `rayleigh`/`stretch` en modo `rgb`, `scale`, `palette`, `expression`, `geographics`), además de `output_file`/`output_width`/`output_height`. Las opciones no usadas simplemente no aparecen.
* **GeoTIFF (`-t`):** solo un subconjunto de estos metadatos se embebe como tags GDAL dentro del archivo: `tool`, `satellite`, `sector`, `band`, `scan_time`, `product` (cuando aplica) y `colormap_min`/`colormap_max`/`colormap_size`/`colormap_units` en pseudocolor. `crs` y `bounds` no se duplican como texto porque el GeoTIFF ya los representa de forma nativa (geotransform + proyección WKT); `command`, `channels` (con min/max/quantity) y `enhancements` solo existen en el JSON sidecar.

**Casos de uso:**
* **Reproducibilidad:** documentación exacta de los parámetros de realce aplicados (gamma, CLAHE, Rayleigh, etc.) y del producto/canal de origen.
* **Integración:** automatización de flujos de visualización (ej. `mapdrawer`), que consume `crs`/`bounds`/`product` para ubicar y clasificar cada imagen.
* **Trazabilidad:** identificar satélite, sector, canal(es), producto y proyección que generaron cada imagen.

### 5.8 Convenciones de salida

Si no se especifica la opción `-o` o `--out`, se genera un nombre determinista basado en los metadatos del archivo "ancla", las bandas y las operaciones aplicadas:

**Formato:** `hpsv_<SAT>[_<SECTOR>]_<YYYYJJJ>_<hhmm>_<COMMAND>_<CH>[_<OPS>].<ext>`

Ejemplo:
  ```bash
  hpsv gray OR_ABI-L1b-RadC-M6C13_G16_s20242190300217.nc
  # → hpsv_G16_conus_2024219_0300_gray_C13.png
  ```
  
### 5.9 Álgebra de bandas y composiciones personalizadas

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

## 6. Detalles técnicos

### 6.1 Geometría y geolocalización

La generación de vistas se apoya en formulaciones geométricas rigurosas. El sistema implementa reproyección directa desde proyección geoestacionaria a malla lat/lon uniforme (WGS84), con manejo de huecos e inferencia automática de dominios fuera del disco visible. El recorte geográfico se optimiza cuando es posible, realizándolo antes de la reproyección.

### 6.2 Corrección atmosférica (Rayleigh)

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

### 6.3 CLAHE

El sistema incluye ecualización adaptativa de histograma con control de contraste local (CLAHE) para mejorar la interpretabilidad visual en escenas con variaciones espaciales pronunciadas de contraste.

### 6.4 Composición True Color

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

### 6.5 Rendimiento

Implementado en C11 (ISO/IEC 9899:2011) con paralelización mediante OpenMP, HPSATVIEWS prioriza el alto rendimiento, el uso eficiente de memoria y la escalabilidad en sistemas multi-núcleo.

El I/O está optimizado en todos los builds, y en una escena de disco completo es
lo que domina el tiempo total una vez que el cómputo está en la GPU.

**Lectura.** Las variables NetCDF se leen tomando los chunks HDF5 crudos y
descomprimiéndolos en paralelo con libdeflate, en vez de pasar por el pipeline de
filtros de un solo hilo de HDF5. Dos refinamientos más pesan a escala de disco
completo: el índice de chunks se recorre **una sola vez** con `H5Dchunk_iter`
(HDF5 ≥ 1.14) en lugar de una búsqueda por chunk —la búsqueda por llamada hace
que el costo total crezca cuadráticamente con el número de chunks, lo que en una
banda de 0.5 km (9216 chunks) significaba 0.68 s solo de recorrer el índice— y
los bytes se leen después con `pread` en paralelo, usando los offsets que ese
mismo recorrido entrega, lo que esquiva el lock global de HDF5. Con HDF5 anterior
se conserva el camino por chunk, más lento pero correcto.

**Escritura.** La salida GeoTIFF se escribe multi-hilo y, por defecto, como un
archivo tileado **sin** la pirámide de overviews (Cloud-Optimized): esa pirámide
es ~90% del costo de escritura y es trabajo desperdiciado cuando el archivo es
intermedio y se recorta aguas abajo. Usa `--cog` para emitir un Cloud Optimized
GeoTIFF completo cuando el GeoTIFF sea el producto final. El dataset GDAL en
memoria envuelve el buffer de píxeles entrelazado que ya existe en vez de
copiarlo a planos por banda, lo que elimina una reserva del tamaño de la imagen y
una pasada de de-interleave hostil a la caché.

**Para escenas grandes, preferir GeoTIFF.** La salida PNG usa una configuración
de compresión/filtro rápida afinada para imagen satelital de alta entropía, pero
libpng sigue comprimiendo en un solo hilo (~90 MB/s medidos). En un disco
completo eso hace que escribir PNG cueste unas 10× más que el GeoTIFF
equivalente: 2.4 s contra 0.22 s en las mediciones de abajo. Usa `-t`/`.tif` para
cualquier cosa de disco completo; el PNG está bien para sectores más chicos.

### 6.6 Aceleración por GPU (CUDA)

Un backend CUDA opcional traslada a una GPU NVIDIA las etapas por píxel más
pesadas. Es **opt-in y no invasivo**: el build por defecto no necesita el toolkit
de CUDA y la ruta OpenMP/CPU sigue siendo la implementación de referencia.
Compila con `make CUDA=1 CUDA_ARCH=sm_XX` y actívalo en ejecución con `--cuda`.

`--cuda` reporta el dispositivo que seleccionó (`CUDA device 0: NVIDIA A30
(sm_80, 56 SMs, 23928/24163 MiB free)`) —conviene conservarlo en los logs de un
servidor compartido— y falla de inmediato, antes de cualquier I/O, si no hay GPU
utilizable o si el binario se compiló para un `CUDA_ARCH` distinto al de la GPU
presente. Ver
[`docs/cuda-support/DEPLOYMENT.md`](docs/cuda-support/DEPLOYMENT.md).

El diseño es **residente en device**: cada canal se sube una vez y toda la
composición se encadena en la GPU —malla lat/lon, geometría de vista, corrección
Rayleigh por LUT, verde sintético, stretch piecewise, composición RGB y, para
`daynite`, el pseudocolor nocturno, la máscara día/noche y la mezcla—. La imagen
compuesta también se queda en device para alimentar la reproyección, así que un
render completo de `daynite -G` mueve los cuatro canales de entrada hacia la GPU
y una imagen de salida, sin viajes intermedios. El *ratio sharpening*
(`--sharpen`) también corre en GPU, fundido en un solo kernel que recalcula el
promedio de cada bloque 2×2 en sitio en vez de materializar los arreglos
intermedios de promedio y razones que construye la CPU. Las opciones sin kernel
(`--ray-analytic`, `--citylights`, otros modos RGB) caen a CPU de forma
transparente.

Conviene revisarlo cuando una configuración parezca más lenta de lo esperado:
`--cuda` registra `this RGB configuration isn't GPU-accelerated yet; using CPU
path` cada vez que una opción saca a truecolor del gate acelerado. Hasta esta
versión `--sharpen` hacía justo eso, lo que convertía en silencio toda
comparación con geo2grid —que exige el realce para igualar su producto— en una
medición de CPU.

#### Resultados

Medido con `reproduction/bench_server.sh`, que compila y cronometra ambos modos
desde la misma revisión del código en el host objetivo: GOES-19 disco completo
L1b, `truecolor --rayleigh`, GeoTIFF tileado por defecto, NVIDIA A30 (sm_80) con
un Xeon de 64 hilos. Los tiempos son del proceso completo, así que incluyen el
arranque y la creación del contexto CUDA.

| Build | Tiempo total |
|---|---:|
| CPU (OpenMP, sin CUDA) | 2.53 s |
| CUDA | **1.17 s** |
| | **2.16×** |

Medir ambos builds desde la misma revisión importa más de lo que parece. A lo
largo de este ciclo el build CPU por sí solo pasó de 3.76 s a 2.53 s, únicamente
por el trabajo de I/O de §6.5, que beneficia a los dos. Contrastar la cifra vieja
de CPU contra la actual de CUDA habría dado ~4× y le habría atribuido a la GPU un
segundo de trabajo que no hizo.

`daynite -G` es el otro producto operativo; en el mismo host renderiza en 0.91 s
con `--cuda` (medido con las marcas del log, así que sin los ~0.2 s de arranque
del proceso que sí incluye la tabla de arriba).

**Por etapa** (el cómputo que reemplaza la GPU, disco completo):

| Etapa | CPU (OpenMP) | CUDA |
|---|---:|---:|
| Geometría de vista (solar + satélite + azimut) | 1.06 s | 0.037 s |
| Malla de navegación lat/lon | 0.146 s | 0.013 s |
| Corrección Rayleigh por LUT (por canal) | 0.11 s | 0.005 s |
| Pseudocolor nocturno (`daynite`) | 0.089 s | 0.012 s |
| Máscara día/noche (`daynite`) | 0.058 s | 0.004 s |
| Reproyección a lat/lon (`-G`/`-B`) | 0.28 s | 0.074 s |

Dos advertencias que conviene decir sin rodeos:

- **Estos números no se transfieren entre máquinas.** En una Tesla T4, cuya
  capacidad en doble precisión es 1/32 de la simple, el mismo código apenas
  supera a una CPU fuerte, porque la navegación y la reproyección son en doble.
  La A30 (1/2) es otra historia. Hay que re-medir en el host objetivo antes de
  activar `--cuda` en producción; `reproduction/bench_server.sh` hace justo eso.
- **Lo que queda del tiempo es I/O, no aritmética.** Tras el trabajo anterior, un
  `daynite -G` de disco completo gasta ~38% leyendo los cuatro canales y ~31%
  codificando el GeoTIFF, y menos del 20% calculando. A la GPU ya le queda poco
  por ganar.

#### Equivalencia numérica

La ruta CPU es la referencia. La salida GPU se verifica contra ella con
`tests/test_cuda.sh` (10 casos; se corre con `CUDA=1 tests/run_all_tests.sh`), y
las diferencias se quedan al nivel del redondeo en punto flotante: salida
idéntica en true color simple y en la reproyección, y para la cadena más larga
(`daynite`) 903 píxeles distintos de 12.8 M, todos de **±1 en la salida de 8
bits** y ninguno sobrevive una tolerancia del 2%. El origen es la contracción FMA
y que la biblioteca matemática de CUDA difiere de glibc en 1–2 ULP. Las
decisiones con umbral, que sí se notarían si discreparan —como la clasificación
día/noche—, coinciden exactamente.

#### Interruptores de escape

Cada optimización de arriba se puede desactivar en ejecución, que es como se
averigua si conviene en un host dado sin recompilar:

| Variable | Fuerza |
|---|---|
| `HPSV_NO_PINNED_UPLOAD=1` | H2D paginable en vez de fijar el buffer de host |
| `HPSV_NO_DEVICE_HANDOFF=1` | volver a subir la imagen para la reproyección |
| `HPSV_NO_PREAD=1` | `H5Dread_chunk` en vez de `pread` paralelo |
| `HPSV_NO_MEM_ZEROCOPY=1` | copiar los píxeles al dataset de GDAL |
| `HPSV_DISABLE_FAST_READ=1` | `nc_get_var` en vez del lector por chunks |

El del pinning es el que más vale la pena revisar: registrar un buffer de 470 MB
cuesta 0.010 s en el host de la A30 pero 0.048 s en una RTX 5060 Ti de
escritorio, donde resulta una pérdida neta.

---

## 7. Estado del proyecto

HPSATVIEWS se encuentra en desarrollo activo, funcional estable y ampliación progresiva de capacidades y documentación.

**Trabajo futuro:** permitir más satélites y no solamente los GOES. El backend
CUDA opcional (§6.6) ya cubre toda la ruta de cómputo de los productos
operativos, incluida la reproyección, así que lo que queda del tiempo en un
render de disco completo lo domina leer el NetCDF y codificar la salida, no la
aritmética. La descompresión NetCDF en GPU —para que los datos decodificados
nazcan en el device— es la palanca principal que resta y está en consideración;
el compuesto con luces de ciudad y los modos RGB menos comunes siguen en CPU.

¿Quieres contribuir, reportar un problema o pedir soporte? Consulta
[CONTRIBUTING.md](CONTRIBUTING.md). El proyecto sigue el
[Código de Conducta](CODE_OF_CONDUCT.md) basado en el Contributor Covenant.

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
  
---

## 9. Cómo citar

Si HPSATVIEWS te resulta útil en tu investigación o software, por favor
cítalo. La metadata de citación (autores, ORCID, versión, licencia) se
mantiene en [CITATION.cff](CITATION.cff) — GitHub genera a partir de este
archivo un botón "Cite this repository" en la página principal del repo.

```bibtex
@software{aguilar_sierra_hpsatviews,
  author    = {Aguilar Sierra, Alejandro},
  title     = {hpsatviews: High Performance Satellite Views},
  version   = {1.0.1},
  year      = {2026},
  publisher = {Zenodo},
  doi       = {10.5281/zenodo.21092353},
  url       = {https://doi.org/10.5281/zenodo.21092353}
}
```

---

## 10. Autor y licencia

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
