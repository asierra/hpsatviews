# Plan: Optimizar Reproyección + Instrumentación de Tiempo

## Contexto

La función `reproject_image_to_geographics` en `src/reprojection.c` agrega ~43s al procesamiento Full Disk con la bandera `-r`.

**Causa raíz**: el enfoque *forward scatter* (fuente→destino) crea huecos inevitables; el relleno de huecos en 5 pasadas es el cuello de botella.

**Solución**: mapeo inverso (destino→fuente) usando la proyección analítica GOES-R. Elimina el relleno de huecos por completo.

## Hechos verificados

### Convenciones de unidades (confirmado en reader_nc.c)
- `lambda_0` (global de módulo) = **RADIANES** (convertido de grados vía `lo_proj_orig / rad2deg`)
- `proj_info.lon_origin` = **GRADOS** (valor crudo del NetCDF, NO convertido) → hay que convertir a radianes antes de usar en trigonometría
- `geotransform[6]` = **RADIANES** (ángulos de escaneo nativos ABI)
- `geotransform[0]` = esquina top-left X (ya con half-pixel offset del centro del píxel)
- `geotransform[1]` = ancho de píxel en radianes (= x_scale del NetCDF)
- `geotransform[3]` = esquina top-left Y (con half-pixel offset)
- `geotransform[5]` = alto de píxel en radianes (negativo para N→S)
- navla/navlo producen lat/lon en **GRADOS**

### Accesibilidad de DataNC en los call sites (confirmado)
- **rgb.c ~línea 902**: `ctx.channels[ctx.ref_channel_idx]` es un `DataNC` completo con `geotransform` + `proj_info`
- **processing.c ~línea 303**: `c01` es un `DataNC` completo con `geotransform` + `proj_info`
- No se necesita pasar parámetros de forma creativa — DataNC ya está a la mano

### Uso de nav grids después de la reproyección (confirmado)
- Después del call a reproject, **solo se usan los escalares `.fmin`/`.fmax`** (para bbox de metadata + geotransform del GeoTIFF)
- Los grids completos `.data_in` NO se leen después de la reproyección
- Los nav grids siguen siendo necesarios **antes** de la reproyección para: corrección Rayleigh, máscara día/noche
- En processing.c, los nav grids se computan condicionalmente (`if has_clip || is_geotiff || do_reprojection`) — así que existen cuando se ejecuta la reproyección

### No existe proyección inversa en el código
- Confirmado: cero coincidencias para latitud geocéntrica, proyección inversa, o código lat/lon→ángulo de escaneo
- Hay que implementar desde cero usando las ecuaciones del GOES-R PUG Volumen 4

---

## Fase 1: Instrumentación de tiempo

Agregar `omp_get_wtime()` (ya se usa OpenMP) antes/después de:
1. El loop de forward-scatter
2. El bloque de relleno de huecos

Imprimir con `LOG_INFO` mostrando segundos transcurridos en cada etapa.

**Archivo**: `src/reprojection.c` — 3 llamadas de timing, 2 prints LOG_INFO.

---

## Fase 2: Reproyección analítica inversa

### Por qué es lenta (causas raíz)
1. Forward scatter crea huecos (píxeles destino sin valor) en los bordes/limbo del disco
2. Relleno de huecos: 5 iteraciones × W_dst×H_dst × 8 vecinos ≈ 1,200M operaciones (Full Disk 2km)
3. `#pragma omp atomic write` por byte en el scatter loop = overhead de sincronización pesado
4. navla/navlo RAM para Full Disk 1km = 2×450 MB = presión masiva de caché en el scatter loop

### Proyección inversa GOES-R PUG (lat/lon → x_rad, y_rad)

Entradas desde `DataNC.proj_info`: `sat_height (H_sat)`, `semi_major (a)`, `semi_minor (b)`, `lon_origin (λ₀ EN GRADOS)`

```
H = a + H_sat
e² = (a² - b²) / a²
λ₀_rad = lon_origin × (π/180)        ← DEBE convertirse de grados a radianes

φ_c = atan((b²/a²) × tan(φ))         // latitud geodésica → geocéntrica
r_c = b / sqrt(1 - e²·cos²(φ_c))

s_x = H - r_c·cos(φ_c)·cos(λ - λ₀_rad)
s_y = -r_c·cos(φ_c)·sin(λ - λ₀_rad)
s_z = r_c·sin(φ_c)

Visibilidad: H·(H - s_x) > s_y² + (a/b)²·s_z²   (visible desde el satélite)

y_rad = atan(s_z / s_x)                           ← ángulo de escaneo N-S
x_rad = asin(-s_y / sqrt(s_x² + s_y² + s_z²))    ← ángulo de escaneo E-W
```

**Píxel fuente desde ángulos de escaneo** usando `DataNC.geotransform`:
```
col = (x_rad - geotransform[0]) / geotransform[1]
row = (y_rad - geotransform[3]) / geotransform[5]
```
Luego interpolar bilinealmente src_image en (col, row).

### Firma de la nueva función
```c
ImageData reproject_image_analytical(
    const ImageData* src_image,
    const DataNC* data_nc,             // provee proj_info + geotransform + dimensiones
    float lat_min, float lat_max,      // extensión geográfica (de navla.fmin/fmax)
    float lon_min, float lon_max,      // extensión geográfica (de navlo.fmin/fmax)
    float native_resolution_km,
    const float* clip_coords           // clip opcional (reemplaza extensión)
);
```
Se pasan las extensiones como floats explícitos — evita depender de los nav grids dentro de la función. El llamador ya tiene `nav_lat.fmin/fmax` y `nav_lon.fmin/fmax`.

### Pasos de implementación (en orden)

**Paso 1**: Agregar timing a `reproject_image_to_geographics` existente (Fase 1)
- Archivo: `src/reprojection.c` — agregar `omp_get_wtime()` antes del scatter, antes del gap fill, después del gap fill
- Propósito: medición baseline + diagnosticar dónde se gasta el tiempo

**Paso 2**: Implementar `reproject_image_analytical()` en `src/reprojection.c`
- Calcular dimensiones de salida (misma lógica que la actual: resolución → conteo de píxeles, limitar a MAX_DIM)
- Para cada píxel destino (y, x) en `#pragma omp parallel for collapse(2)`:
  - Calcular lat = lat_max - y × res_deg, lon = lon_min + x × res_deg
  - Convertir lat/lon (grados) → radianes
  - Aplicar proyección inversa GOES-R PUG → (x_rad, y_rad)
  - Verificación de visibilidad → si no es visible, píxel = 0 (negro)
  - Convertir ángulos de escaneo a píxel fuente: col, row desde geotransform
  - Verificación de límites sobre dimensiones de src_image
  - Interpolación bilineal de src_image en (col, row)
- No se necesita relleno de huecos — cada píxel de salida se computa directamente
- Agregar LOG_INFO de timing para toda la operación

**Paso 3**: Agregar declaración en `include/reprojection.h`

**Paso 4**: Actualizar call site en `src/rgb.c` (~línea 902)
- Reemplazar `reproject_image_to_geographics(...)` con:
  `reproject_image_analytical(&ctx.final_image, &ctx.channels[ctx.ref_channel_idx], ctx.nav_lat.fmin, ctx.nav_lat.fmax, ctx.nav_lon.fmin, ctx.nav_lon.fmax, ..., clip_coords)`

**Paso 5**: Actualizar call site en `src/processing.c` (~línea 303)
- Reemplazar con: `reproject_image_analytical(&final_image, &c01, navla_full.fmin, navla_full.fmax, navlo_full.fmin, navlo_full.fmax, ..., clip_coords)`

**Paso 6**: Conservar la función vieja sin borrar — se puede remover después de validación

### Archivos a modificar
- `src/reprojection.c` — timing de Fase 1 + implementación de nueva función
- `include/reprojection.h` — declaración de nueva función
- `src/rgb.c` (~L902) — actualizar call site
- `src/processing.c` (~L303) — actualizar call site

### Verificación
1. `make` — compila sin errores
2. Sin `-r`: verificar que la salida no cambia (no se ejercita el código de reproyección)
3. Con `-r -v`: LOG_INFO de timing muestra tiempo de una pasada; comparar salida visual vs versión actual
4. `time ../bin/hpsv rgb <archivo_full_disk> -t -r -v` — objetivo: ≤5s para reproyección vs ~43s actuales
5. Con `-r -c <clip>`: verificar que clip funciona con la nueva función
6. `cd tests && ./run_all_tests.sh` — tests de regresión pasan
7. Salida GeoTIFF (si aplica): verificar que la geotransform del metadata es correcta

### Decisiones tomadas
- **Conservar función vieja**: no borrar `reproject_image_to_geographics` — dejar como fallback/referencia
- **Interpolación bilineal**: mejor calidad que nearest-neighbor, costo despreciable vs eliminar gap fill
- **Extensiones como parámetros**: pasar fmin/fmax explícitamente en vez de calcular analíticamente desde el borde del disco — más simple, y los valores siempre están disponibles en los call sites
- **Conversión lon_origin**: confirmado que está en GRADOS en proj_info → hay que convertir a radianes en la nueva función
