---
marp: true
theme: default
paginate: true
math: katex
style: |
  section.title {
    text-align: center;
  }
  section.title h1 {
    font-size: 2.2em;
  }
  code { font-size: 0.85em; }
  table { font-size: 0.82em; }
  section.small { font-size: 1.2em; }
  section.small h2 { font-size: 1.5em; }
---

<!-- _class: title -->

# HPSATVIEWS

### Visualización de datos satelitales de alto rendimiento

**Alejandro Aguilar Sierra** — `asierra@unam.mx`
Laboratorio Nacional de Observación de la Tierra, UNAM

Licencia: GPL v3 · C11 · v1.0.0

---

## Motivación y contexto

- Los satélites GOES-R generan **16 bandas espectrales** cada 10 min (Full Disk)
- Las herramientas existentes (geo2grid, satpy) son flexibles, pero lentas para visualización operativa
- Un flujo **rápido, reproducible y sin dependencias pesadas** es útil, sobre todo
para minimizar el tiempo de entrega oportuna de productos relevantes para prevención
de desastres.

---

### Filosofía de diseño

- Opera exclusivamente en el dominio de **vistas y productos visuales**
- No pretende sustituir plataformas de análisis físico ni herramientas GIS, ni una herramienta genérica como polar2grid y geo2grid
- Flujo muy simple: **archivo ancla → procesamiento → imagen + metadatos**
- El énfasis es velocidad para visualización operativa, no análisis cuantitativo general 

---

## Conceptos clave

| Concepto | Definición |
|----------|-----------|
| **Imagen** | Colección de bandas que registran la distribución espacial y espectral de magnitudes físicas (radiancia, temperatura, reflectancia) |
| **Vista** | Representación derivada de una imagen, normalizada y cuantizada para el sistema visual humano |
| **Producto** | Vista asociada a un concepto físico identificable: *true color*, *air mass*, *ash*, etc. |
| **Instante** | Momento temporal de la observación, codificado como `YYYYJJJHHMM` (año, día juliano, hora, minuto) |

El **archivo ancla** NetCDF identifica la escena; HPSATVIEWS infiere automáticamente las bandas necesarias.

---

## Arquitectura técnica

- **Lenguaje:** C11 (ISO/IEC 9899:2011) con extensiones POSIX
- **Paralelismo:** OpenMP — escalable en sistemas multi-núcleo
- **Compilación nativa:** optimización para la arquitectura del host (`-march=native`)

### Dependencias

| Biblioteca | Función |
|-----------|---------|
| **libnetcdf** | Lectura de archivos GOES L1b/L2 (NetCDF4 + compresión) |
| **libpng** | Escritura de imágenes PNG, lectura de fondo citylights |
| **GDAL** | Escritura de GeoTIFF COG y transformaciones de coordenadas |
| **OpenMP** | Paralelización automática de bucles de procesamiento |

---

## Uso y comandos

```bash
hpsv <comando> <archivo_ancla> [opciones]
```

### `gray` — Escala de grises (monocromático)

```bash
hpsv gray OR_ABI-L1b-RadF-M6C13_G16.nc
```

### `pseudocolor` — Mapa de colores (para cada intensidad un color)

```bash
hpsv pseudocolor -p paleta.cpt archivo_GOES.nc
```

### `rgb` — Composición RGB

```bash
hpsv rgb -m <modo> -archivo.nc
```

---

## Opciones destacadas

| Opción | Descripción |
|--------|------------|
| `-c, --clip <zona>` | Recorte geográfico: clave predefinida o coordenadas explícitas |
| `--clahe` | Ecualización adaptativa de histograma (CLAHE) |
| `-g, --gamma <val>` | Corrección gamma |
| `-t, --geotiff` | Salida en Cloud Optimized GeoTIFF (COG) |
| `-G, --geographics` | Reproyección a coordenadas geográficas equirrectangulares |
| `-s, --scale <n>` | Escala espacial (entero, positivo amplía, negativo reduce) |
| `--stretch` | Estiramiento por tramos tipo geo2grid |

---

### Nombres de salida por omisión

Si no se usa la opción `-o`, el nombre se genera **automáticamente** y de forma determinista:

```
hpsv_<SAT>_<YYYYJJJ>_<hhmm>_<COMMAND>_<CH>[_<OPS>].png
```
```bash
hpsv gray OR_ABI-L1b-RadF-M6C13_G16_s20242190300217.nc
# → hpsv_G16_2024219_0300_gray_C13.png
# → hpsv_G16_2024219_0300_gray_C13.json  (metadatos)
```

---

### Nombres de salida con plantillas

Con `-o` se puede usar una plantilla con marcadores:

| Marcador | Valor | Marcador | Valor |
|----------|-------|----------|-------|
| `{YYYY}` | Año 4 dígitos | `{YY}` | Año 2 dígitos |
| `{MM}` | Mes | `{DD}` | Día |
| `{hh}` | Hora | `{mm}` | Minuto |
| `{ss}` | Segundo | `{JJJ}` | Día juliano |
| `{TS}` | Instante `YYYYJJJhhmm` | `{CH}` | Canal (`C13`, etc.) |
| `{SAT}` | Satélite (`G16`, `G19`) | | |

```bash
hpsv gray -o "ir_{SAT}_{CH}_{YYYY}{MM}{DD}.png" \
      OR_ABI-L1b-RadF-M6C13_G19_s20253551801183.nc
# → ir_G19_C13_20251221.png
```

---

## Álgebra de bandas

Permite composiciones al vuelo con expresiones lineales de bandas con las opciones `--expr` y `--minmax`.

- Términos con coeficientes por banda: `2.0*C13`
- Operadores entre términos: `+`, `-`, `0.5*C02+0.3*C03`
- Rangos opcionales (si se omiten, se auto-calculan)

```bash
# Monocanal (gray/pseudocolor)
hpsv gray --expr "C13-C15" --minmax "0.0,100.0" archivo.nc

# RGB personalizado (separar R;G;B con punto y coma, entre comillas)
hpsv rgb --mode custom \
  --expr "C13-C14;C13-C11;C13" \
  --minmax "-2,2;-4,2;240,300" \
  -o "ceniza_volcanica.png" archivo.nc
```

---

## Ejemplo: realce de ceniza volcánica



---

## Composición RGB 

Genera un compuesto RGB a partir de combinaciones lineales de bandas.

### Modos RGB disponibles

| Modo | Canales necesarios | Uso |
|------|---------|-----|
| `truecolor` | C01, C02, C03 | Color natural diurno |
| `night` | C13 | Escenas nocturnas con luces de ciudad |
| `daynite` | Mezcla auto | Día/noche con máscara solar |
| `ash` | C13−C15, C14−C11, C13 | Detección de ceniza volcánica |
| `airmass` | C08−C10, C12−C13, C08 | Masas de aire |
| `custom` | Álgebra de bandas | Composiciones personalizadas |

---

## Composición Truecolor

Imagen de _color verdadero_ a partir de tres canales ABI: C01 (0.47 µm, azul), C02 (0.64 µm, rojo) y C03 (0.865 µm, infrarrojo cercano). 

ABI no tiene canal verde nativo; se sintetiza:

$$G = 0.465 \cdot B_{C01} + 0.465 \cdot R_{C02} + 0.07 \cdot NIR_{C03}$$

### Realces necesarios

- **Corrección Rayleigh**: Remueve la contribución de dispersión molecular de la atmósfera en canales visibles, mejorando la fidelidad cromática de escenas diurnas.
- **Relajación en nubes**: Donde C02 ≥ 0.20, la corrección Rayleigh se atenúa linealmente hasta anularse en C02 = 1.0, evitando sobre-corrección en nubes brillantes y superficies muy reflectivas.
- **Piecewise stretch** (`--stretch`): Expande tonos oscuros y comprime claros para mejorar la diferenciación tonal. Curva equivalente a geo2grid.

---

## Comparativa sin y con Rayleigh

---

<!-- _class: small -->

## Corrección Rayleigh analítica (`--ray-analytic`)

**Modelo físico por píxel** — sin tablas externas, calculado en tiempo real.

**1. Espesor óptico** — Bucholtz (1995):

$$\tau_R(\lambda) = \frac{0.008569}{\lambda^4} \left(1 + \frac{0.0113}{\lambda^2} + \frac{0.00013}{\lambda^4}\right)$$

**2. Función de fase** — Hansen & Travis (1974) con despolarización ($\rho_n = 0.0279$):

$$P(\Theta) = \frac{0.75}{1+2\gamma}\left[(1+3\gamma) + (1-\gamma)\cos^2\Theta\right]$$

**3. Reflectancia Rayleigh** y corrección:

$$\rho_R = \frac{\tau_R \cdot P(\Theta)}{4\,\mu_s\,\mu_v} \qquad \rho_{\text{corr}} = \rho_{\text{obs}} - \rho_R$$

**Salvaguardas:**
- SZA > 85° → píxel enmascarado (noche/terminador)
- $\rho_{\text{corr}} < 0$ → clamp a $10^{-4}$
- C02 ≥ 0.20 → relajación lineal (nubes brillantes)

<!-- La implementación LUT usa las mismas salvaguardas pero indexa tablas pre-computadas de pyspectral (interpolación trilineal en secantes de SZA, VZA y azimut relativo). -->

---

<!-- _class: small -->

## Corrección Rayleigh con LUTs de pyspectral (`--rayleigh`)

**Tablas pre-calculadas** de [pyspectral](https://doi.org/10.5281/zenodo.1205534) (Scheirer et al., 2018) para C01, C02, C03 — embebidas en el binario en compilación.

### Pipeline

1. Calcular SZA, VZA, RAA por píxel desde geometría GOES
2. Convertir ángulos a **secantes** (como hace pyspectral)
3. **Interpolación trilineal** en el cubo $[\sec\theta_s \times \sec\theta_v \times \varphi_{rel}]$
4. Restar reflectancia Rayleigh al canal: $\rho_{\text{corr}} = \rho_{\text{obs}} - \rho_R^{\text{LUT}}$

### Bugs resueltos después de mucho esfuerzo

| Problema | Síntoma | Corrección |
|----------|---------|-----------|
| Escala LUT × τ en lugar de × 100 | Nubes amarillas en bordes | Valores ya son reflectancia directa |
| Eje azimuth con Δφ en lugar de 180°−Δφ | 30–35% sub-corrección | Convención pyspectral: `180 − azidiff` |
| Sin relajación en nubes | Sobre-corrección en cúmulos | C02 ≥ 0.20 → atenuación lineal |

**Resultado final**: gap medio con geo2grid de ±0.8–2.0 DN (vs. +4–10 antes de las correcciones).

<!-- Estos bugfixes se documentaron en ANALISIS_RAYLEIGH_DISCREPANCIA.md, RESUMEN_ANALISIS_RAYLEIGH.md y CORRECCION_AZIMUTH_CLOUD_RELAXATION.md -->

---

## Híbrido DayNite

Combinación de los modos RGBs `truecolor` y `night` usando una máscara precisa con base en la geometría solar. Es el modo RGB por omisión.

<img src="daynite.png" style="max-height:70%">


---

## Comparativa Full Disk

## Comparativa CONUS

---

## Rendimiento DayNite

<style scoped>
.two-columns {
  display: flex;
  justify-content: space-between;
  gap: 40px;
  margin-top: 20px;
  font-size: 0.7em;
}
.column {
  flex: 1;
  display: flex;
  flex-direction: column;
}
.column:first-child {
  border-right: 2px solid #eee;
  padding-right: 30px;
}
.column pre {
  font-size: 0.7em;
  padding: 10px;
  background-color: #f6f8fa;
}
.column code {
  font-size: 0.7em;
  color: #d63384;
}
.total-time {
  font-size: 0.9em;
  font-weight: bold;
  color: #d9534f;
  text-align: center;
  margin-top: auto;
}
.total-time-fast {
  font-size: 0.9em;
  font-weight: bold;
  color: #5cb85c;
  text-align: center;
  margin-top: auto;
}
</style>

<div class="two-columns">

<div class="column">

### Flujo actual (geo2grid + GDAL)

**1. Generación True Color:**
`geo2grid.sh -r abi_l1b -w geotiff -p true_color`
```text
real    2m8.304s
user    7m6.399s
sys     0m57.155s
````

**2. Reescalamiento al 25%:**
`gdal_translate -r bilinear -outsize 25% 25% ...`

```text
real    0m34.278s
user    0m32.835s
sys     0m1.428s
```

**3. Composición con lado nocturno: (~16s)**

<div class="total-time">Tiempo Total Real: ~3 min </div>

</div>

<div class="column">

### Flujo HPSATVIEWS

**Generación en un solo paso:**
`hpsv rgb archivo_goes.nc`

```text
real    0m22.521s
user    1m7.778s
sys     0m4.999s
```

<div class="total-time-fast">Tiempo Total Real: ~22 seg</div>

</div>

</div>


---

## Metadatos JSON sidecar

Cada imagen genera automáticamente un archivo JSON de trazabilidad:

```json
{
  "tool": "hpsatviews",
  "version": "1.0",
  "command": "rgb",
  "mode": "truecolor",
  "satellite": "GOES-16",
  "timestamp": "2024-08-07T18:01:17Z",
  "channels": ["C01", "C02", "C03"],
  "processing": {
    "gamma": 1.0,
    "clahe_applied": true,
    "rayleigh_corrected": true
  },
  "geometry": {
    "projection": "geographics",
    "bounds": [-110.5, 15.0, -90.0, 30.0]
  }
}
```

- **Reproducibilidad:** parámetros exactos documentados
- **Integración:** automatización de flujos (ej. mapdrawer)
- **Trazabilidad:** auditoría para publicaciones científicas

<!-- El JSON se nombra automáticamente: salida.png → salida.json -->

---

## Rendimiento

### Por qué es rápido

- **C11 nativo** — sin intérprete, sin runtime, sin overhead de GC
- **OpenMP** — bucles de procesamiento paralelos (`#pragma omp parallel for`)
- **Compilación `-march=native`** — aprovecha instrucciones SIMD del host
- **Memoria controlada** — allocación explícita, sin copias intermedias innecesarias

### Orden de magnitud

| Operación | Escala típica |
|-----------|--------------|
| Gray / pseudocolor (Full Disk) | ~1-2 s |
| True color con Rayleigh | ~3-5 s |
| Reproyección a lat/lon | ~2-4 s |
| COG GeoTIFF con overviews | ~1-3 s |

Tiempos en sistema multi-núcleo típico. Filtrables en logs con:

```bash
hpsv rgb -v ... 2>&1 | grep '\[PERF\]'
```

---

## Referencias 

- Bah et al. (2018) — GOES-16 ABI True Color Imagery, NOAA/CIMSS
- Bucholtz (1995) — Rayleigh scattering calculations, *Applied Optics*
- Hansen & Travis (1974) — Light scattering, *Space Science Reviews*
- Scheirer et al. (2018) — Atmospheric correction, *Remote Sensing*
- Zuiderveld (1994) — CLAHE, *Graphics Gems IV*
- Lira Chávez (2010) — Tratamiento digital de imágenes multiespectrales, UNAM

### Más información

`hpsv --help` · Código fuente: licencia GPL v3

<!-- Cerrar invitando a probar la herramienta con datos de ejemplo del repositorio -->
