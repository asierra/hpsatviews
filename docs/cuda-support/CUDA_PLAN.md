# Plan de implementación: soporte CUDA en hpsatviews

## Contexto

`hpsatviews` es un proyecto en C11 + OpenMP para procesamiento de imágenes
satelitales GOES-R ABI (composiciones RGB, escalas de grises, corrección
Rayleigh, reproyección geoestacionaria → lat/lon, CLAHE, etc). El objetivo es
acelerar con CUDA las operaciones más pesadas, manteniendo la versión
CPU/OpenMP como default y como referencia de corrección.

**Hardware objetivo:** NVIDIA RTX 5060 Ti (Blackwell, `sm_120`), en la
estación de trabajo. **Ojo:** la máquina de desarrollo donde se integró este
código NO tiene `nvcc` ni GPU (verificado 2026-07-12); todo lo que requiere
compilar `.cu` o ejecutar en GPU se valida en la estación.

**Filosofía:** cada kernel es un módulo aislado en `src/cuda/`, seleccionable
en runtime con `--cuda`, que reemplaza a su contraparte OpenMP sin tocar la
lógica de negocio. Cero regresiones en el build por defecto: `make` sin
`CUDA=1` compila exactamente igual que antes y no requiere `nvcc`.

## Estado actual (2026-07-12): integrado al árbol, pendiente de validar en GPU

Los borradores que vivían en esta carpeta ya se integraron (mejorados) al
árbol del proyecto:

| Pieza | Ubicación | Notas |
|---|---|---|
| Build opcional `make CUDA=1` | `Makefile` | `CUDA_ARCH ?= sm_120`, `CUDA_HOME ?= /usr/local/cuda` (ambos sobreescribibles en la línea de make). La regla nvcc genera `.d` de dependencias (`-MMD -MF`, requiere CUDA ≥ 11.2) y respeta `DEBUG=1` (`-g -G`). |
| Header público | `include/cuda_kernels.h` | Firmas `extern "C"` idénticas a las de `gray.h`; solo se incluye bajo `#ifdef HPSV_CUDA`. |
| Primer kernel: gray | `src/cuda/gray_cuda.cu` | Puerto de `create_single_gray()`. Un hilo por píxel, bloque 16×16. Mejoras sobre el borrador original: `CUDA_CHECK` ahora hace `goto cleanup` (el borrador retornaba fugando `imout.data`, `d_in`, `d_out` y los eventos); warm-up `cudaFree(0)` para que la creación del contexto CUDA (cientos de ms la primera vez) no contamine el `LOG_TIMING`; se eliminó el `cudaDeviceSynchronize` redundante (el memcpy D2H ya sincroniza). |
| Flag CLI `--cuda` | `src/main.c` (`add_common_opts`), `src/config.c` (`use_cuda` + validación), `include/config.h` | Común a los 3 comandos. Sin build CUDA: error claro y exit≠0 ("rebuild with make CUDA=1"). Con build CUDA pero comando `rgb`: `LOG_WARN` (aún no hay kernels RGB) y sigue por CPU. Datos byte (int8): `LOG_WARN` y CPU. |
| Despacho runtime | `src/processing.c` (sitio único de llamada de `create_single_gray`) | `#ifdef HPSV_CUDA` + `cfg->use_cuda`. |
| Ayuda EN/ES | `include/help_en.h`, `include/help_es.h` | Ambos idiomas en sincronía (requisito del proyecto). |
| Test de equivalencia | `tests/test_cuda.sh`, registrado en `tests/run_all_tests.sh` | Compara salida `--cuda` vs CPU con `compare_image.sh` (gray IR invertido, gray+alfa bpp=2, pseudocolor con paleta interna). Se salta con éxito si el binario no tiene CUDA o no hay GPU, así que no rompe CI ni entornos sin GPU. `CUDA=1 tests/run_all_tests.sh` compila con CUDA y ejecuta la suite de verdad. |

Notas de corrección verificadas contra el código:

- `NonData = 1.0e+32` (`src/datanc.c:19`) satisface `IS_NONDATA` (≥ 1e30),
  así que el único chequeo `is_nondata_dev()` del kernel equivale al doble
  chequeo `!= NonData && !IS_NONDATA` del original. No hay divergencia.
- `image_create(0,0,0)` devuelve `data == NULL`, que es exactamente lo que
  `run_processing()` verifica como fallo (`processing.c`), así que el
  sentinela de error del kernel encaja con el flujo existente.

> **Advertencia (2026-07-12):** ninguno de los cambios integrados pudo
> compilarse en la laptop de desarrollo: tras la actualización del sistema
> de ese día faltan paquetes de build (al menos `libwebp-dev`; revisar
> también `libnetcdf-dev`, `libpng-dev`, `libgdal-dev`). El código C se
> revisó solo por inspección. Primer paso de la siguiente sesión:
> reinstalar dependencias, `make` y `tests/run_all_tests.sh` para confirmar
> cero regresiones en el build por defecto (el flag `--cuda` debe fallar
> con mensaje claro y la suite CUDA debe saltarse sola).

## Validación y benchmark en GPU (2026-07-13, RTX 5060 Ti, CUDA 13.3 sm_120)

Validado en la estación: `make CUDA=1` compila, `CUDA=1 tests/run_all_tests.sh`
pasa las 8 suites, equivalencia CPU/GPU **0 píxeles distintos** en todos los
casos (IR invertido, visible, alfa bpp=2, pseudocolor, gamma).

**Benchmark del kernel gray standalone** (full-disk L1b RadF GOES-19, OpenMP 6
cores vs CUDA incl. transferencias):

| Canal | Píxeles | CPU OpenMP | CUDA (incl. transf.) | Ratio |
|---|---|---|---|---|
| C13 FD 5424² | 29 MP | ~0.020 s | 0.042 s | 2.1× más lento |
| C01 FD 10848² | 118 MP | 0.087 s | 0.165 s | 1.9× más lento |
| C02 FD 21696² | 471 MP | 0.399 s | 0.701 s | 1.8× más lento |

**Hallazgo:** el kernel gray es ~2× **más lento** que OpenMP, y el ratio se
mantiene estable en un rango de 16× de tamaño → es transferencia H2D/D2H +
`cudaMalloc` escalando lineal con los datos, no overhead fijo amortizable. Un
kernel trivial por-píxel nunca gana: pagas un round-trip de GB por microsegundos
de cómputo. **La GPU solo gana si la transferencia se amortiza sobre varias ops
encadenadas en device.**

### Capa "DataF residente en device" (implementada 2026-07-13)

Prerrequisito para que cualquier kernel Nivel 1 valga la pena. Sube el float una
sola vez, encadena kernels en GPU, baja solo el uint8 de salida.

| Pieza | Ubicación |
|---|---|
| Tipo `DataFDev` + API (`upload`/`destroy`/`apply_gamma`/`gray_from_dev`) | `include/cuda_dataf.h` |
| Lifecycle + kernel gamma residente | `src/cuda/dataf_dev.cu` |
| `create_single_gray_from_dev` (reusa `gray_kernel`, sin H2D) | `src/cuda/gray_cuda.cu` |
| Helper device compartido (`is_nondata_dev`) | `src/cuda/cuda_common.cuh` |
| Despacho de cadena `upload → gamma → gray → download` | `src/processing.c` (bloque float bajo `--cuda`) |
| Caso de test gamma | `tests/test_cuda.sh` |

Cada op loguea su propio `[PERF]` para exponer el desglose. **Prueba de
amortización** (C02 FD 471 MP, cadena `upload → gamma → gray`):

| Op | Costo en la cadena | Costo si fuera aislada |
|---|---|---|
| upload (cudaMalloc + H2D) | 0.393 s (1×) | 0.393 s |
| gamma (kernel) | **0.009 s** | ~0.65 s (round-trip propio) |
| gray + D2H | 0.254 s | 0.254 s + H2D |

La gamma residente cuesta 9 ms; aislada costaría ~70× más por su propio
round-trip. **Agregar una op a la cadena cuesta solo su kernel.** Para truecolor
(~10 ops sobre 3 bandas) esto convierte ~10 transferencias en 1 upload + 1
download. El kernel gray standalone sigue perdiendo contra OpenMP; el objetivo
no es acelerar gray, sino tener la infra para que la cadena RGB completa gane.

> **Nota de build:** cambiar entre `make` y `make CUDA=1` requiere `make clean`
> primero — make no recompila los `.o` de C al cambiar solo `CFLAGS`
> (`-DHPSV_CUDA`), así que un build mezclado enlaza `config.c` sin CUDA y
> `--cuda` falla con "built without CUDA support" aunque el binario tenga los
> kernels. `make clean && make CUDA=1` siempre.

### True color completo residente (implementado 2026-07-13)

El truecolor **por defecto** y con **`--rayleigh`** corren enteros en GPU
(`compose_truecolor_cuda` en `src/rgb.c`, gate en `run_rgb`): sube C01/C02/C03
(+ sza/vza/raa si hay Rayleigh) una vez, encadena solar → LUT → green → gamma →
compose en device, baja una imagen RGB. Kernels en `src/cuda/truecolor_cuda.cu`
y `src/cuda/rayleigh_cuda.cu`. Fallback a CPU con `LOG_WARN` para
`--ray-analytic`, sharpen, stretch, modos no-truecolor y custom. Equivalencia
0 px (CONUS: default, +gamma, +rayleigh).

### Navegación (geometría de vista) residente (implementado 2026-07-13)

La navegación para Rayleigh (sza/vza/raa) se computaba en CPU y era el ~8 s
restante del wall-time. Portada a device (`src/cuda/nav_cuda.cu` +
`include/cuda_nav.h`): kernels `solar_kernel` / `satellite_kernel` /
`relaz_kernel` computan los ángulos desde las mallas lat/lon ya subidas.

Optimización clave: casi toda la geometría solar de `compute_sun_geometry`
(`src/reader_nc.c`) depende **solo del tiempo**, no del píxel. Se extrajo
`solar_ephemeris()` (parte solo-tiempo → `SolarEphemeris`) y
`sun_angles_from_ephemeris()` (parte por-píxel); el CPU queda idéntico (0 px
diff verificado) y el kernel recibe la efeméride como 3 escalares, evitando ~20
trig por píxel. Helpers de host `reader_solar_ephemeris_from_file` /
`reader_read_satellite_params` exponen los parámetros. Guard: si lat/lon no está
a la resolución del canal o falla algo, se cae a compose CPU completo (Rayleigh
nunca se pierde). Todo en `double` para casar con CPU.

**Benchmark full-disk truecolor `--rayleigh`** (ref C01, 10848²=118 MP,
RTX 5060 Ti vs OpenMP 6 cores):

| Sección | CPU | CUDA |
|---|---|---|
| Navegación (solar+sat+raa) | ~8 s | **0.325 s** |
| Rayleigh LUT (por canal) | ~1.1 s | 0.006 s |
| Composición completa | ~3 s | ~0.9 s |
| **Wall-time end-to-end** | 41–42 s | **31 s** |

Con la navegación en GPU el wall-time baja **~10.5 s (~25%)**. El cómputo dejó de
importar: los ~31 s restantes son **I/O puro** (leer 3 NetCDF de full-disk +
escribir PNG). 0 px diff (CONUS) en toda la cadena truecolor+rayleigh+nav.

### I/O: escritura PNG (optimizado 2026-07-13, CPU)

Desglose del wall-time full-disk (~31 s): leer C01/C02/C03 ~5.5 s (C02 21696²
domina, ~3.7 s), downsample C02→1 km ~1.5 s, cómputo GPU ~1 s, y **escribir el
PNG ~21 s** (10848²×3 = 336 MB crudos → 143 MB). El PNG era ~70% del wall-time.

Causa: `src/writer_png.c` usaba los defaults de libpng — zlib nivel 6 y
**filtrado adaptativo** (prueba los 5 filtros por fila), un hilo. En imagen
satelital (alta entropía) subir el nivel casi no reduce tamaño pero cuesta
carísimo (nivel 9 = 65 s para 140 MB vs nivel 6 = 21 s para 143 MB). Fix:
`png_set_compression_level(png, 1)` + `png_set_filter(png, 0, PNG_FILTER_SUB)`
(NONE para paleta, donde filtrar índices es contraproducente). Solo cambia los
bytes comprimidos, no los píxeles (suites 0 diff).

| Config PNG | Escritura | Tamaño |
|---|---|---|
| nivel 6 + adaptativo (antes) | ~21 s | 143 MB |
| nivel 1 + NONE | ~4 s | 176 MB |
| **nivel 1 + SUB (ahora)** | **~4 s** | **151 MB** |

**Wall-time full-disk truecolor `--rayleigh --cuda`: 31 s → 13.7 s** (2.3× vs el
CPU original de 42 s → 3×). Alternativa: el GeoTIFF COG (ZSTD, `src/writer_geotiff.c`)
ya escribía en ~12 s.

### I/O: lectura NetCDF paralela (implementado 2026-07-13, CPU)

La lectura de los 3 NetCDF (~5.5 s) era descompresión HDF5/zlib de **un solo
hilo** (no I/O de disco — con page cache la lectura cruda es ~0.07 s; se descartó
el ramdisk por eso). Los archivos usan chunks (226×226) con filtro shuffle+deflate.
`src/reader_nc_chunk.c` lee los chunks crudos con `H5Dread_chunk` y los descomprime
en paralelo con **libdeflate** (ya enlazado vía GDAL; ~2× más rápido que zlib por
hilo), invierte el shuffle y hace scatter al grid. Sortea el lock global de HDF5:
la lectura cruda es trivial, la descompresión (lo caro) es nuestra y paralela.
Fallback a `nc_get_var` ante cualquier layout no soportado (`HPSV_DISABLE_FAST_READ=1`
lo fuerza). Descarta ~el mismo `datatmp` int16 que `nc_get_var`, así que la
conversión a float downstream no cambia.

| Canal | Descompresión antes | Ahora (libdeflate ‖) |
|---|---|---|
| C01 10848² | ~1.0 s | 0.10 s |
| C02 21696² | ~3.7 s | 0.39 s (~9.4×) |
| C03 10848² | ~0.9 s | 0.08 s |

Equivalencia: PNG **byte-idéntico** fast vs fallback en full-disk (mismo md5) +
suites 9/9 (el fast-path se ejerce en todos los tests, validado contra goldens).

**Wall-time full-disk truecolor `--rayleigh --cuda`: 13.7 s → ~10.2 s.**

### Balance acumulado

Full-disk truecolor `--rayleigh`: **~42 s (CPU original) → ~10.2 s (~4×)**, atacando
en orden el cuello real en cada paso: composición (Rayleigh 180×) → navegación
(~8 s → 0.3 s) → escritura PNG (~21 s → ~4 s) → lectura NetCDF (~5.5 s → ~0.6 s).
El cómputo ya es marginal; el resto (~10 s) se reparte entre descompresión de
lectura, downsample, escritura y misc — sin un único dominante. El final purista
pendiente sería nvCOMP (descompresión en GPU → datos nacen en device, sin H2D).

## Pendiente — checklist para la estación de trabajo (RTX 5060 Ti)

0. **En la laptop, antes de nada:** `sudo apt-get install libnetcdf-dev
   libpng-dev libgdal-dev libwebp-dev`, luego `make` y
   `tests/run_all_tests.sh` (build por defecto, sin CUDA).
1. **Compilar:** `make CUDA=1`. Si `nvcc --version` < 12.8, `sm_120` no
   existe todavía: usar `make CUDA=1 CUDA_ARCH=sm_89` (funcionalmente
   correcto, sin features específicas de Blackwell). Si el toolkit no está
   en `/usr/local/cuda`, pasar `CUDA_HOME=/ruta`.
2. **Validar equivalencia:** `CUDA=1 tests/run_all_tests.sh` (o
   `cd tests && ./test_cuda.sh` con el binario ya compilado). Si hay
   diferencias de píxeles > tolerancia, revisar primero redondeo
   float→uint8 (CPU trunca igual que el kernel, pero `-O3 -march=native`
   puede fusionar multiplicaciones con FMA y diferir ±1 LSB — la tolerancia
   de `compare_image.sh` lo absorbe).
3. **Medir:** correr con `-v` sobre un full-disk real y comparar los
   `[PERF]` de `Single Gray` (OpenMP) vs `Single Gray (CUDA, incl.
   transferencias)`. El baseline justo es OpenMP multi-hilo, no serial.
   Si la transferencia H2D/D2H domina, anotar el desglose antes de
   optimizar kernels (afecta la prioridad de todo el roadmap).
4. **Documentar:** agregar `--cuda` y `make CUDA=1` al README y a las
   páginas man (`man/hpsv.1`, `man/hpsv.es.1`) antes de mergear a `main`.

## Roadmap de kernels (ubicaciones verificadas en el código)

### Nivel 1 — por-píxel independientes (sin dependencia entre hilos)

| Función origen | Ubicación real | Estado | Notas |
|---|---|---|---|
| `create_single_gray()` | `src/gray.c` | ✅ portado (residente) | `create_single_gray_from_dev`; opera sobre `DataFDev`. |
| Corrección gamma | `dataf_apply_gamma()` en `src/datanc.c` (llamada desde `processing.c` y `rgb.c`) | ✅ portado (residente) | `dataf_dev_apply_gamma`, in place sobre `DataFDev`. Cuesta 9 ms en la cadena (471 MP): prueba de amortización. Falta la ruta gamma de `rgb.c` (3 canales). |
| Álgebra de bandas (`--expr`) | `dataf_op_dataf()` / `dataf_op_scalar()` en `src/datanc.c` (el parser en `parse_expr.c` solo produce el `LinearCombo`; la evaluación son ops elemento-a-elemento) | Pendiente | No hace falta compilar expresiones a kernels: basta portar las 2 ops elemento-a-elemento, o un kernel único que evalúe el `LinearCombo` (≤10 términos) por píxel, que además evita N pasadas por memoria. |
| Composición RGB / green sintético | `src/truecolor.c` | ✅ portado (residente) | `create_truecolor_green_from_dev` + `create_multiband_rgb_from_dev` (`src/cuda/truecolor_cuda.cu`). El truecolor **por defecto** corre entero en GPU vía `compose_truecolor_cuda` (`src/rgb.c`): sube C01/C02/C03 una vez, green + gamma×3 + compose en device, baja una imagen RGB. Modos/flags avanzados (Rayleigh/sharpen/stretch/custom, no-truecolor) → `LOG_WARN` + CPU. 0 px diff (default, +gamma, y fallback de rayleigh). |
| `create_single_grayb()` (byte) | `src/gray.c` | Pendiente (hoy: WARN + CPU) | Solo si aparece un caso de uso L2-byte pesado; probablemente no vale la pena. |

### Nivel 2 — stencil / vecindad local (memoria compartida)

| Función origen | Ubicación real | Estado | Notas |
|---|---|---|---|
| CLAHE | `image_apply_clahe()` en `src/image.c` | Pendiente | Histograma por tile (atomics/shared) + interpolación bilineal entre tiles. Referencia: PMPP, caps. de shared memory e histogramas. |
| Ratio sharpening | `dataf_ratio_sharpen_map()` en `src/truecolor.c:236` + `dataf_mean_2x2()` en `src/datanc.c` | Pendiente | Media por bloque 2×2 → tiling clásico con shared memory. |

### Nivel 3 — gather / lookup (mayor beneficio potencial, el más delicado)

| Función origen | Ubicación real | Estado | Notas |
|---|---|---|---|
| Reproyección geos → lat/lon | `src/reprojection.c` | Pendiente | Gather con acceso fuente no coalescido. Candidato ideal a memoria de textura (interpolación bilineal por hardware). Cuidado con el llenado de nodata fuera del disco (ver Gotchas del proyecto). |
| Corrección Rayleigh (LUT) + solar zenith | `src/rayleigh.c`, `src/truecolor.c` | ✅ portado (residente) | `luts_rayleigh_correction_dev` + `apply_solar_zenith_correction_dev` (`src/cuda/rayleigh_cuda.cu` + `include/cuda_rayleigh.h`). La LUT se parsea en host (se expuso `rayleigh_lut_load_from_memory`) y su tabla (~65 KB) sube a memoria global; lookup trilineal por hilo. Se encadena sobre los `DataFDev` ya subidos en `compose_truecolor_cuda`: solar×3 → LUT(C01, redband=C02) → LUT(C02). **Kernel: 0.124 s (CPU 13 MP) → 0.006 s (GPU 118 MP)**, ~180×. 0 px diff (CONUS). El truecolor `--rayleigh` corre entero en GPU; solo `--ray-analytic`/sharpen/stretch siguen en CPU. **Primera cadena RGB que gana a OpenMP.** |

Para cada kernel nuevo, repetir el patrón ya establecido: firma drop-in en
`cuda_kernels.h`, implementación en `src/cuda/`, despacho `#ifdef HPSV_CUDA +
cfg->use_cuda` en el sitio de llamada, y caso de comparación CPU/GPU en
`tests/test_cuda.sh`.

## Consideraciones transversales

- **Layout:** `DataF`/`DataB`/`ImageData` ya son buffers planos row-major
  (`y*width+x`) — ningún cambio de layout necesario.
- **Transferencias:** medir siempre transferencia + cómputo (los
  `cudaEvent_t` del kernel gray ya lo hacen). Cuando haya varios kernels en
  cadena (gamma → gray → CLAHE), la ganancia grande está en mantener los datos
  en GPU entre pasos en vez de ida y vuelta por operación — el `DataF`
  "residente en device" (`DataFDev`, `include/cuda_dataf.h`) ya existe y es la
  base de todo lo que sigue. El benchmark 2026-07-13 confirmó que sin él ningún
  kernel Nivel 1 gana; con él, cada op extra en la cadena cuesta solo su kernel.
- **Doble ruta:** la ruta OpenMP no se retira; es el baseline de corrección
  y el fallback de todo entorno sin GPU.
- **Criterio de aceptación por kernel:** salida equivalente a la versión
  OpenMP sobre datos reales de `sample_data/` dentro de la tolerancia de
  `compare_image.sh` — no solo "compila y corre".
- **Métrica de rendimiento:** CUDA vs OpenMP multi-hilo real, no vs serial.

## Referencias

- Hwu, Kirk, El Hajj — *Programming Massively Parallel Processors*, 5ª ed.
  Caps. de jerarquía de memoria, coalescencia, shared memory, histogramas
  y textura aplican directamente a los niveles 2 y 3.

## Historial

- Los borradores originales de esta carpeta (`gray_cuda.cu`,
  `cuda_kernels.h`, `hpsatviews_makefile.diff`) se integraron al árbol el
  2026-07-12 con las correcciones descritas arriba y se eliminaron de aquí
  para evitar duplicados desincronizados.
