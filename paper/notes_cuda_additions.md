# Notas: cómo integrar los avances de CUDA + I/O al paper de SoftwareX

Borradores LaTeX listos para pegar en `hpsatviews_softwarex.tex`. Los números de
GPU van marcados con `%% TODO-A30` — se llenan con `reproduction/bench_server.sh`
en el servidor A30 (Xeon Gold 6326 vs A30, misma máquina, columnas CPU y CUDA).
Los de **I/O son CPU-side** y ya se pueden medir en cualquier build sin GPU.

Resumen de dónde toca cada avance:

| Avance | Sección del paper | Tipo de cambio |
|---|---|---|
| Backend CUDA opcional (residente en device) | Architecture + Functionalities + Impact | nuevo párrafo/bullet |
| Lectura NetCDF paralela (libdeflate) | Architecture + Impact | frase + tabla |
| GeoTIFF rápido por defecto (`--cog`) | Functionalities + pipeline | edición de bullet |
| Benchmarks | Impact | tabla nueva |
| Versión y dependencias | Code metadata (Tabla 1) | edición |
| "planned GPU" → "implementado" | Impact (cierre) | reescritura |

---

## 1. Code metadata (Tabla 1)

- **C1** `v1.0.1` → **`v1.1.0`** (nueva funcionalidad opcional CUDA + rework de I/O).
  Recordar bumpear `include/version.h` (lo checa `tests/test_json.sh`).
- **C6** añadir CUDA opcional y libdeflate:
  ```
  C11 (POSIX extensions), OpenMP, optional CUDA, GNU Make
  ```
- **C7** dependencias:
  ```
  OpenMP-capable GCC; \texttt{libnetcdf}, \texttt{libhdf5}, \texttt{libdeflate},
  \texttt{libpng}, \texttt{libgdal}, \texttt{libwebp}. Optional GPU path:
  NVIDIA CUDA toolkit and a CUDA-capable GPU. Linux/POSIX.
  ```

---

## 2. Software architecture — párrafo nuevo (tras el párrafo de OpenMP, ~línea 227)

```latex
Two paths coexist by design. The OpenMP/CPU implementation is the reference and
the default build; an optional CUDA backend (\texttt{make CUDA=1}, selected at
run time with \texttt{--cuda}) offloads the compute-heavy stages to an NVIDIA
GPU. Rather than accelerating one kernel in isolation---where the
host--device transfer would dominate---the backend keeps each channel resident
on the device and chains the whole composition on the GPU: viewing geometry,
Rayleigh lookup, synthetic-green and RGB compositing, gamma, and geometric
reprojection, so the transfer cost is paid once per scene. The CPU path remains
the bit-for-bit correctness reference and the fallback for any mode or option
without a GPU kernel. Independently of the GPU, input decoding is parallelized:
NetCDF variables are read by pulling the raw HDF5 chunks and decompressing them
across all cores with \texttt{libdeflate}, bypassing the single-threaded HDF5
filter pipeline that otherwise dominates full-disk load time.
```

También editar el paso 6 del pipeline (línea ~201) para el GeoTIFF rápido:
```latex
    \item Write the final image as PNG or GeoTIFF. GeoTIFF output is
    multi-threaded and, by default, a fast tiled file without the
    Cloud-Optimized overview pyramid (which is wasted work when the file is an
    intermediate that is cropped downstream); a full Cloud Optimized GeoTIFF is
    available on demand. Essential metadata is embedded in the GeoTIFF tags, and
    a JSON sidecar is emitted conditionally.
```

---

## 3. Software functionalities — bullet nuevo (en el itemize, ~línea 272)

```latex
    \item \textbf{Optional GPU acceleration.} A CUDA backend, built optionally
    (\texttt{make CUDA=1}) and selected at run time (\texttt{--cuda}), offloads
    the compute-heavy stages---viewing geometry, Rayleigh correction, true-color
    composition and geometric reprojection---to an NVIDIA GPU, keeping the data
    resident on the device across the pipeline. Output is validated identical to
    the CPU reference; unsupported modes fall back to the CPU transparently.
```

Y editar el bullet de salida georreferenciada (línea ~272) para mencionar el
default rápido y `--cog` (o dejarlo al pipeline; basta uno de los dos).

---

## 4. Impact — párrafo de rendimiento + tabla (insertar antes del cierre, ~línea 399)

```latex
Beyond deployment ergonomics, the engine's parallel I/O and optional GPU
backend cut per-scene wall time on full-disk scenes. Reading and decompressing
the input NetCDF in parallel with \texttt{libdeflate} reduces variable
decompression by roughly $9\times$ over the single-threaded HDF5 path, and
writing a fast tiled GeoTIFF instead of a full Cloud-Optimized pyramid removes
the dominant share of the output cost---both benefit the CPU-only build. With
the CUDA backend, the compute-heavy stages are accelerated further, as
summarized in Table~\ref{tab:perf}; the CPU/OpenMP path remains the reference and
produces identical output.

\begin{table}[!ht]
\centering
\caption{Full-disk (10{,}848$\times$10{,}848) true-color with Rayleigh
correction on \PRODSERVER: CPU/OpenMP build vs.\ optional CUDA build. Output is
validated identical between paths.}
\label{tab:perf}
\begin{tabular}{@{}lrr@{}}
\toprule
\textbf{Stage} & \textbf{CPU (OpenMP)} & \textbf{CUDA} \\
\midrule
Viewing geometry (solar + satellite)   & \TODO & \TODO \\
Rayleigh LUT correction (per channel)  & \TODO & \TODO \\
Geometric reprojection to lat/lon      & \TODO & \TODO \\
\midrule
End-to-end wall time                   & \TODO & \TODO \\
\bottomrule
\end{tabular}
\end{table}

%% TODO-A30: llenar \TODO con reproduction/bench_server.sh en el servidor A30
%% (\PRODSERVER = "an NVIDIA A30 with an Intel Xeon Gold 6326"). Definir macros:
%%   \newcommand{\PRODSERVER}{an NVIDIA A30 (Intel Xeon Gold 6326)}
%%   \newcommand{\TODO}{\fbox{??}}
```

### Reescritura del cierre de Impact (líneas ~399-403)

Actual: *"Planned development focuses on ... exploring fine-grained GPU
parallelism for Rayleigh atmospheric correction and geometric reprojection."*

Nuevo (ya implementado):
```latex
The software reached its first public release (v1.0.0) in June 2026, archived
on Zenodo under a persistent DOI. The optional GPU backend now accelerates the
Rayleigh correction, viewing-geometry, compositing and reprojection stages that
were previously identified as the most computationally intensive; ongoing work
extends support to additional satellite platforms beyond the GOES-R series and
explores GPU-side NetCDF decompression so decoded data is born on the device.
```

---

## Números provisionales (caja de dev: RTX 5060 Ti vs 6 cores)

Referencia mientras llegan los de la A30 (NO poner en el paper; el servidor A30
puede dar valores muy distintos: CPU más fuerte, GPU A30 con FP64 alto):

- Lectura: descompresión C02 (471 MP) 3.7 s → 0.39 s (~9x, CPU-side).
- Nav (solar+sat+azimut) full-disk: CPU (6 cores) ~8 s → CUDA 0.33 s.
- Rayleigh LUT/canal: CPU ~1.1 s → CUDA 0.006 s.
- Reproyección: CPU 2.28 s → CUDA 0.30 s (~7.5x).
- Wall full-disk truecolor+rayleigh (GeoTIFF): CPU-build ~19 s → CUDA-build ~9.2 s.

**Ojo para el paper:** reportar todo en UNA máquina (el servidor A30) con ambas
columnas de `bench_server.sh`, no mezclar la caja de dev.
```
