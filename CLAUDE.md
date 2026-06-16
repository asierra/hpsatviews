# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Release build (HPC optimized: -O3 -march=native)
make

# Debug build with symbols (binary: bin/hpsv_debug)
make DEBUG=1

# Spanish language build
make HPSV_LANG=es

# System-wide install
sudo make install

# Clean build artifacts
make clean
```

Dependencies: `libnetcdf-dev`, `libpng-dev`, `libgdal-dev`, OpenMP-capable gcc.

## Tests

```bash
# Run full test suite
cd tests && ./run_all_tests.sh

# Run individual test
cd tests && ./test_rgb.sh
```

Test scripts: `test_rgb.sh`, `test_pseudo.sh`, `test_clahe.sh`, `test_daynite.sh`, `test_rayleigh.sh`, `test_config.sh`. Sample data lives in `sample_data/` (GOES-16 ABI CONUS L2 CMI NetCDF files).

## Architecture

### Processing Pipeline

Entry point `src/main.c` dispatches to command handlers. Flow:

1. **Parse args** → `src/args.c`: Custom parser supporting subcommands (`gray`, `pseudocolor`, `rgb`)
2. **Load NetCDF** → `src/reader_nc.c`: Reads GOES L1b (Rad) or L2 (CMI/LST/SST/etc.) products
3. **Apply corrections** → Rayleigh (`src/rayleigh.c`), gamma, CLAHE (`src/image.c`)
4. **Normalize to 8-bit** → `src/gray.c` or `src/truecolor.c`
5. **Optional reproject** → `src/reprojection.c`: Fixed grid to geographic (lat/lon equirectangular)
6. **Write output** → `src/writer_png.c` or `src/writer_geotiff.c`, plus JSON sidecar via `src/metadata.c`

### Core Types

- `DataF` (`include/datanc.h`): Float grid for satellite radiance/physical data. Allocate with `dataf_create()`, free with `dataf_destroy()`. Use `IS_NONDATA()` macro for fill-value checks.
- `ImageData` (`include/image.h`): 8-bit RGB/grayscale for output. Allocate with `image_create()`.
- `ChannelSet` (`include/channelset.h`): Multi-channel bundle for RGB modes.
- `ProcessConfig` (`include/config.h`): Immutable config struct built from parsed args — always use this, never ad-hoc params. Parsed in `src/config.c`.
- `MetadataContext`: Mutable context built as processing proceeds.

### Filename Inference

Anchor file (`OR_ABI-L1b-RadF-M6C13_G16_s20253231800172...nc`) identifies the scene. `find_id_from_name()` extracts the timestamp signature (`s20253231800`), then sibling channels are located by replacing `C13` → `C01`, `C02`, etc. in the same directory. See `src/channelset.c`.

### RGB Mode System

Modes defined in `src/rgb.c` switch: `truecolor`, `night`, `ash`, `airmass`, `daynite`, `severestorm`, `so2`, `custom`. Each specifies channel combinations and per-channel linear algebra. `daynite` auto-blends day/night using solar geometry from `src/daynight_mask.c`.

True color synthesizes a green channel not present in ABI: `G = 0.465*B + 0.465*R + 0.07*NIR`.

### Rayleigh Correction

Two implementations in `src/rayleigh.c`:
- **LUT** (`--rayleigh`, default): Lookup tables from pyspectral, embedded in binary via `src/rayleigh_lut_embedded.c` (regenerated with `assets/embed_luts.py`). Indexed by `sec(SZA)`, `sec(VZA)`, `180° − Δφ` (azimuth convention matches pyspectral).
- **Analytic** (`--ray-analytic`): Bucholtz (1995) model + Hansen & Travis (1974) phase function. Lighter, less accurate.

Both apply cloud relaxation: correction fades to zero when C02 reflectance exceeds 0.20 (linear rolloff to 1.0).

## Conventions

- **Language**: C11 with POSIX extensions (`-std=c11 -D_POSIX_C_SOURCE=200809L`)
- **Naming**: `snake_case` for functions, `PascalCase` for types
- **Error handling**: Return `int` (0=success, non-zero=error) or NULL pointer on failure
- **Logging**: `LOG_ERROR()`, `LOG_WARN()`, `LOG_INFO()` from `include/logger.h`. Verbose mode (`-v`) activates `LOG_INFO`.
- **No global state**: Pass context structs explicitly (`ProcessConfig`, `MetadataContext`, `RgbContext`)
- **OpenMP**: Most pixel loops use `#pragma omp parallel for`. Always use `reduction()` for aggregates (min/max/sum).

## Common Extension Tasks

**New RGB mode**: Add to the switch in `src/rgb.c`, add channel algebra in `src/truecolor.c` or inline, update help text in `include/help_en.h` and `include/help_es.h`, document in README section 4.6.

**New processing option**: Add flag to `ProcessConfig` in `include/config.h`, parse in `config_from_argparser()` (`src/config.c`), apply in pipeline (`src/processing.c` or `src/rgb.c`).

**Regenerate Rayleigh LUTs**: Run `assets/embed_luts.py` (requires pyspectral), then rebuild.

## Debugging

- Inspect NetCDF file structure: `ncdump -h file.nc`
- Compare output against geo2grid/GDAL reference: `tests/compara_gdal.sh`
- Validate GeoTIFF output: `tests/valida_geotiff.py`
- Active TODO: `docs/TODO.txt`

## Contexto técnico
- Proyecto en C con paralelización OpenMP
- Compilador: gcc con flags -fopenmp
- Patrones usados: parallel for, reduction, critical sections
- Problema actual: posibles race conditions en variables compartidas
