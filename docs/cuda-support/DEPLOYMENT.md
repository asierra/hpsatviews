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
defecto) con desglose por etapa. **Vigila la línea `Navigation (CUDA)`**: es el
kernel double-heavy más sensible al FP64 de la GPU (en dev, RTX 5060 Ti: ~0.33 s).

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
