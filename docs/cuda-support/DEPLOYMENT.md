# Deployment del soporte CUDA de hpsatviews

Guía para compilar, validar y decidir si activar `--cuda` en un servidor de
producción. **Regla de oro: los benchmarks de desarrollo NO transfieren.** El
resultado depende mucho del CPU (núcleos/hilos) y de la GPU (throughput FP64,
generación de PCIe, ancho de banda de memoria). Siempre **re-medir en el servidor
real** antes de activar `--cuda`.

## 1. Arquitectura GPU (`CUDA_ARCH`)

El Makefile compila para `CUDA_ARCH ?= sm_120` (RTX 50xx). Ajústalo a tu GPU:

| GPU | `CUDA_ARCH` | Notas |
|---|---|---|
| Tesla T4 | `sm_75` | Turing. **FP64 flojo (~1:32)** → nav/reproyección (double) lentas. |
| A30 / A100 | `sm_80` | Ampere. **FP64 fuerte** + HBM2 + PCIe 4.0 → ideal para este workload. |
| RTX 30xx / A10 | `sm_86` | |
| RTX 40xx | `sm_89` | |
| H100 | `sm_90` | |
| RTX 50xx | `sm_120` | Default; requiere CUDA ≥ 12.8. |

`nvidia-smi` muestra el **driver** (y su CUDA máx), no `nvcc`. El toolkit (nvcc)
se instala aparte. Cualquier CUDA ≥ 11 soporta `sm_75`/`sm_80`.

No hace falta adivinar si acertaste: al resolver `--cuda`, `hpsv` reporta el
dispositivo que agarró y verifica que el binario tenga código para él, antes de
leer ningún NetCDF.

```
INFO : CUDA device 0: Tesla T4 (sm_75, 40 SMs, 14912/15360 MiB free)
```

Con la arch equivocada (p.ej. el build por defecto `sm_120` en una T4) la corrida
aborta ahí mismo, con exit ≠ 0, en vez de fallar a media pipeline:

```
ERROR: --cuda: this binary has no device code for Tesla T4 (sm_75): no kernel
       image is available for execution on the device. Rebuild with
       'make clean && make CUDA=1 CUDA_ARCH=sm_75'.
```

Ojo: `-arch=sm_XX` también embebe PTX, así que un binario compilado para una arch
**menor** que la GPU sí corre (el driver lo JIT-compila, pagando ~100 ms al primer
kernel). Lo que nunca funciona es al revés: PTX de `compute_120` no baja a `sm_75`.
Compila para la arch exacta del servidor.

## 2. Dependencias por distro

**Debian/Ubuntu:**
```bash
sudo apt-get install -y libnetcdf-dev libhdf5-dev libdeflate-dev \
                        libpng-dev libgdal-dev libwebp-dev
```
**RHEL / Rocky / Fedora** (GDAL/netcdf vienen de EPEL + CRB):
```bash
sudo dnf install -y epel-release
sudo dnf config-manager --set-enabled crb
sudo dnf install -y gcc make netcdf-devel hdf5-devel libdeflate-devel \
                    libpng-devel gdal-devel libwebp-devel
```
El nombre del lib HDF5 difiere (`libhdf5_serial` en Debian, `libhdf5` en RHEL). El
Makefile lo **autodetecta**; si falla el link, fuérzalo con `make HDF5_LIB=hdf5`.

Instalar el **CUDA Toolkit** (nvcc) para el build CUDA — en RHEL/Rocky:
```bash
sudo dnf config-manager --add-repo \
  https://developer.download.nvidia.com/compute/cuda/repos/rhel10/x86_64/cuda-rhel10.repo
sudo dnf install -y cuda-toolkit-12-6   # o la 12.x disponible
```

## 3. Compilar y validar

```bash
# Build CUDA para la GPU del servidor (ej. A30):
make clean && make CUDA=1 CUDA_ARCH=sm_80

# Correctitud en ESA GPU (debe dar 9/9, 0 px — valida que el double casa con CPU):
CUDA=1 CUDA_ARCH=sm_80 tests/run_all_tests.sh
```
Cambiar entre `make` y `make CUDA=1` requiere `make clean` primero (make no
recompila los `.o` de C ante un cambio solo de CFLAGS).

## 4. Benchmark del workload real

```bash
reproduction/bench_server.sh /ruta/OR_ABI-L1b-RadF-M6C02_...nc
# Overrides: CUDA_ARCH=sm_80  HDF5_LIB=hdf5  OMP_NUM_THREADS=<hilos de producción>
```
Compara build CPU vs build CUDA (full-disk truecolor + Rayleigh, GeoTIFF por
defecto) con desglose por etapa. **Vigila la línea
`Solar+satellite geometry (CUDA)`**: es el kernel double-heavy más sensible al
FP64 de la GPU (en dev, RTX 5060 Ti: ~0.33 s en full-disk).

Cada etapa acelerada emite un `[PERF]` (nivel DEBUG, requiere `-v`) en **ambas**
rutas, con etiquetas pareadas para poder dividir una entre otra:

| Etapa | `[PERF]` CPU | `[PERF]` CUDA |
|---|---|---|
| Geometría solar+satelital | `Solar geometry` + `Satellite geometry` | `Solar+satellite geometry (CUDA…)` |
| Corrección cenital solar | `Solar zenith correction` | `Solar zenith correction (CUDA…)` |
| Rayleigh LUT | `Rayleigh LUT C01 (… px)` | `Rayleigh LUT C01 (… px grid, CUDA…)` |
| Verde sintético | `Synthetic green` | `Synthetic green (CUDA…)` |
| Composición RGB | `Multiband RGB` | `Multiband RGB (CUDA…: kernel + D2H)` |
| Gray / pseudocolor | `Single Gray` | `Single Gray (CUDA…: kernel + D2H)` |
| Gamma | `Gamma` | `Gamma (CUDA…)` |
| Reproyección | `Analytic reprojection finished` | `Analytic reprojection (CUDA…)` |
| Transferencia H2D | — (no aplica) | `DataF upload (cudaMalloc + H2D)` |

Dos trampas al leer el desglose:

- **`Navigation (WxH)` no es una etapa acelerada.** Es la malla lat/lon
  (`reader_nc.c`) y corre en CPU en los dos builds, así que aparece en ambos logs
  sin cambiar. La contraparte del kernel de navegación son `Solar geometry` +
  `Satellite geometry`, no esta línea.
- **Los `px` de Rayleigh no son el mismo conteo.** El CPU cuenta píxeles válidos
  (salta NonData/noche); el CUDA reporta el grid completo (lanza un hilo por
  píxel). Compara los tiempos, no los conteos.

Suma las etapas aceleradas de cada lado y compáralas contra el wall: si el bloque
acelerado ya es una fracción chica del wall, el techo del speedup es bajo por
Amdahl aunque la GPU gane 10× en su parte (el resto —NetCDF, lat/lon, GeoTIFF—
es CPU en ambos builds).

## 5. Criterio de decisión

Activa `--cuda` en producción **solo si** en el servidor: (a) la suite da 9/9 en
esa GPU, y (b) el wall CUDA gana consistentemente al wall CPU **bajo el
`OMP_NUM_THREADS` y la contención de GPU reales**. Si empata o la GPU está
disputada, el **build CPU** es la opción robusta: igual se lleva las optimizaciones
de I/O (lectura NetCDF paralela con libdeflate, GeoTIFF tileado sin overviews) que
benefician a ambos builds.

Notas de recursos compartidos:
- CPU: hpsv-OpenMP toma todos los hilos por defecto; acótalo con
  `OMP_NUM_THREADS` para ser buen vecino (y mide con ese valor, no con todos).
- GPU: un full-disk truecolor sube 3 canales + nav (~2–3 GB pico). Si la GPU es
  compartida, serializa (un `--cuda` a la vez) para latencia predecible.

## 6. Servidores conocidos

| Servidor | CPU | GPU | `CUDA_ARCH` | Distro / lib HDF5 |
|---|---|---|---|---|
| tsom04 | Xeon Gold 6226R (32 hilos) | Tesla T4, compartida | `sm_75` | (verificar) |
| A30 (ESC4000-E10) | Xeon Gold 6326 (Ice Lake) | A30, **dedicada** | `sm_80` | Rocky 10 → `hdf5` |

Para el workload objetivo (full-disk truecolor+Rayleigh, GeoTIFF): la **A30 es el
mejor candidato** (FP64 fuerte, dedicada). En la T4 el `--cuda` podría solo
empatar por su FP64 flojo — medir antes de decidir.
