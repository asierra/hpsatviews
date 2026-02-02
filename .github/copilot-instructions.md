# HPSATVIEWS AI Agent Instructions

## Project Overview

**HPSATVIEWS** is a high-performance C11 satellite visualization system for GOES-R geostationary satellites. It generates grayscale, pseudocolor, and RGB composite views from NetCDF satellite data in seconds. Optimized for speed with OpenMP parallelization and native architecture compilation.

## Architecture

### Command Structure
Three main commands: `gray`, `pseudocolor`, `rgb`
- Entry: [src/main.c](../src/main.c) dispatches to command handlers
- Flow: ArgParser → ProcessConfig (immutable) → MetadataContext (mutable) → processing
- Config centralized in [include/config.h](../include/config.h) - **always use ProcessConfig**, never ad-hoc params

### Core Data Types
- `DataF`: Float grid for satellite radiance/physical data ([include/datanc.h](../include/datanc.h))
- `ImageData`: 8-bit RGB/grayscale for output ([include/image.h](../include/image.h))
- `ChannelSet`: Multi-channel bundle for RGB modes ([include/channelset.h](../include/channelset.h))

### Processing Pipeline
1. **Parse args** → [src/args.c](../src/args.c): Custom parser supporting subcommands
2. **Load NetCDF** → [src/reader_nc.c](../src/reader_nc.c): Reads GOES L1b (Rad) or L2 (CMI) products
3. **Apply corrections** → Rayleigh ([src/rayleigh.c](../src/rayleigh.c)), gamma, CLAHE
4. **Normalize to 8-bit** → [src/gray.c](../src/gray.c) or [src/truecolor.c](../src/truecolor.c)
5. **Optional reproject** → [src/reprojection.c](../src/reprojection.c): Fixed earth to geographic
6. **Write output** → [src/writer_png.c](../src/writer_png.c) or [src/writer_geotiff.c](../src/writer_geotiff.c)

## Key Patterns

### 1. Memory Management
- Allocate with `dataf_create()`, `image_create()` — never raw malloc for these types
- Always call matching `_destroy()` functions, safe for NULL/uninitialized
- NetCDF handles managed manually: check return codes, close with `nc_close(ncid)`

### 2. OpenMP Parallelization
- Most image/data loops use `#pragma omp parallel for`
- **Always use `reduction()` for aggregates** (min/max/sum) — see [src/reader_nc.c:361](../src/reader_nc.c#L361)
- Mark shared/private variables explicitly in critical sections

### 3. RGB Mode System
- Modes defined in [src/rgb.c](../src/rgb.c): `truecolor`, `night`, `ash`, `airmass`, `daynite`, etc.
- Each mode has specific channel combinations (e.g., `ash` = `[C15-C13, C14-C11, C13]`)
- `daynite` auto-blends day/night using solar geometry mask ([src/daynight_mask.c](../src/daynight_mask.c))

### 4. Filename Inference
- Anchor file identifies scene: `OR_ABI-L1b-RadF-M6C13_G16_s20253231800172...nc`
- Extract timestamp signature (`s20253231800`) with `find_id_from_name()` 
- Find other channels by replacing `C13` → `C01`, `C02`, etc. in same directory
- See [src/channelset.c](../src/channelset.c) for implementation

### 5. Coordinate Systems
- Default: Fixed Grid projection (native GOES)
- Reprojection: `-r` flag converts to Geographic (lat/lon equirectangular)
- Clipping: Specified in geographic coordinates, applied post-reproject if `-r` used

## Build & Test

```bash
# Release build (HPC optimized)
make

# Debug build with symbols
make DEBUG=1

# Spanish language version
make HPSV_LANG=es

# Install system-wide
sudo make install

# Run tests
cd tests && ./run_all_tests.sh
```

- Makefile targets: `all`, `debug`, `install`, `uninstall`, `clean`
- Dependencies: `libnetcdf`, `libpng`, `gdal`, OpenMP-capable gcc
- Output binary: [bin/hpsv](../bin/hpsv)

## Common Tasks

### Adding a New RGB Mode
1. Define channel list in `rgb.c:run_rgb()` switch statement
2. Add channel combination logic (linear algebra in [src/truecolor.c](../src/truecolor.c) or similar)
3. Update help text in [include/help_en.h](../include/help_en.h)
4. Document in [README.md](../README.md) section 4.6

### Adding Enhancement Options
1. Add flag to `ProcessConfig` in [include/config.h](../include/config.h)
2. Parse in `config_from_argparser()` ([src/config.c](../src/config.c))
3. Apply in image processing pipeline ([src/processing.c](../src/processing.c) or [src/rgb.c](../src/rgb.c))
4. Enhancement functions live in [src/image.c](../src/image.c) (CLAHE, histogram equalization)

### Debugging Data Issues
- Enable verbose: `-v` flag activates `LOG_INFO()` messages
- Check NetCDF structure: `ncdump -h file.nc`
- Validate NonData handling: Use `IS_NONDATA()` macro ([include/datanc.h](../include/datanc.h))
- Compare with geo2grid: [tests/compara_gdal.sh](../tests/compara_gdal.sh)

## Project Conventions

- **Language**: C11 with POSIX extensions (`-std=c11 -D_POSIX_C_SOURCE=200809L`)
- **Naming**: `snake_case` for functions, `PascalCase` for types
- **Error handling**: Return `int` (0=success, non-zero=error) or NULL pointers
- **Logging**: Use `LOG_ERROR()`, `LOG_WARN()`, `LOG_INFO()` from [include/logger.h](../include/logger.h)
- **No global state**: Pass context structs explicitly (ProcessConfig, MetadataContext, RgbContext)

## External Dependencies

- **NetCDF-C**: Reading GOES NetCDF4 files with compression
- **libpng**: PNG encoding/decoding (citylights background)
- **GDAL**: GeoTIFF writing and coordinate transformations
- **OpenMP**: Automatic parallelization (`-fopenmp` flag required)

## Satellite Data Details

- **GOES-R ABI**: 16 spectral channels (C01-C16), 0.5km-2km resolution
- **Products**: L1b (calibrated radiance), L2 (derived products like CMI, LST, SST)
- **Coordinates**: Fixed Grid (x/y in radians), Geographic (lat/lon), or native projection
- **Time format**: YYYYJJJHHMM where JJJ is day-of-year

## Important Notes

- **Rayleigh correction**: Two implementations — LUT-based (accurate, default) and analytic (faster)
- **Embedded LUTs**: Pre-generated lookup tables in [src/rayleigh_lut_embedded.c](../src/rayleigh_lut_embedded.c)
- **Metadata**: JSON sidecar output with `--json` flag ([src/metadata.c](../src/metadata.c))
- **Band algebra**: Custom mode allows expressions like `"0.5*C02+0.3*C03"` ([src/parse_expr.c](../src/parse_expr.c))

## Documentation

- User guide: [README.md](../README.md)
- Implementation plans: [docs/](../docs/) — see `plan_*.md` for feature designs
- Analysis docs: `ANALISIS_*.md`, `INFORME_*.md` for algorithm validation
- TODO: [docs/TODO.txt](../docs/TODO.txt) — active development tasks
