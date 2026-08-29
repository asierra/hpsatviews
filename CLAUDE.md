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

Dependencies (Debian/Ubuntu): `libnetcdf-dev`, `libhdf5-dev`, `libdeflate-dev`, `libpng-dev`, `libgdal-dev`, `libwebp-dev`, OpenMP-capable gcc. (`libhdf5-dev`/`libdeflate-dev` back the parallel chunk reader in `src/reader_nc_chunk.c`.) On RHEL/Rocky/Fedora the packages are `netcdf-devel hdf5-devel libdeflate-devel libpng-devel gdal-devel libwebp-devel` (GDAL/netcdf via EPEL). The Makefile auto-detects the HDF5 C library name (`libhdf5_serial` on Debian vs `libhdf5` on RHEL); override with `make HDF5_LIB=hdf5` if detection is wrong.

CUDA build: `make CUDA=1 CUDA_ARCH=sm_XX` where `sm_XX` matches the GPU — `sm_75` (Tesla T4), `sm_80` (A30/A100), `sm_86` (RTX 30xx/A10), `sm_89` (RTX 40xx), `sm_90` (H100), `sm_120` (RTX 50xx, the default; needs CUDA ≥ 12.8). Switching between plain `make` and `make CUDA=1` needs a `make clean` first (make doesn't rebuild C objects on a CFLAGS-only change). `reproduction/bench_server.sh <anchor.nc>` benchmarks CPU-build vs CUDA-build on a target server (the dev speedups don't transfer — re-measure per host).

## Tests

```bash
# Run full test suite (works from any directory; resolves repo root itself)
tests/run_all_tests.sh

# Same, but actually exercising the GPU path (see below — without CUDA=1 the
# CUDA suite silently skips and still counts as passed)
CUDA=1 CUDA_ARCH=sm_XX tests/run_all_tests.sh

# Run an individual test
cd tests && ./test_rgb.sh
```

These are end-to-end regression tests: each script runs `hpsv` on `sample_data/` and most also verify output content with `tests/compare_image.sh`, a tolerant pixel diff (ImageMagick `compare -metric AE -fuzz 2%`, default 1% of pixels allowed to differ) against reference PNGs/GeoTIFFs committed under `tests/expected_output/` (excepted from the root `*.png`/`*.tif` gitignore rules via `tests/.gitignore`). For `.tif`/`.tiff` inputs, `compare_image.sh` forces page `[0]` on both operands (COG outputs embed overview pyramids as extra TIFF pages that otherwise confuse `compare`/`identify`) and strips libtiff "Unknown field with tag..." warnings (unrecognized GeoTIFF private tags) before parsing the AE value. `run_all_tests.sh` aggregates per-suite pass/fail counts. Test scripts: `test_rgb.sh` (exact pixel diff only for `truecolor`; `night`/`ash`/`daynite` and both Rayleigh variants get a lightweight `check_nonblank` — ImageMagick `identify -format "%[standard-deviation]"` must be `>0` — since maintaining an exact reference per mode isn't worth it, but a blank/degenerate output should still fail; `airmass`/`severestorm`/`so2` aren't exercised because their channels, C05/C07–C10/C12, aren't in `sample_data/`), `test_pseudo.sh`, `test_clahe.sh`, `test_geotiff.sh` (pixel diff + GDAL metadata via `strings | grep`, since `GDALSetMetadataItem()` embeds metadata as readable XML inside the TIFF — no GDAL CLI tools needed), `test_reprojection.sh` (`-G`; also checks a corner pixel directly to catch the nodata-fill regression — see Gotchas), `test_json.sh` (sidecar key/value checks via `grep`, including that `version` matches `include/version.h`), `test_config.sh` (parser-only: most cases append `--help`, so it checks flag acceptance, not pipeline behavior), `test_fastread.sh`, `test_cuda.sh` (10 cases comparing every GPU path against its CPU reference; two of them use `cmp` for byte-identity rather than the tolerant diff, because they compare two routes that must produce the *same memory* — the device handoff, and `HPSV_NO_DEVICE_HANDOFF=1`).

`compare_image.sh` also fails when the two images differ in **dimensions** (`compare` crops to the intersection instead of erroring), and parses the AE as a full float including scientific notation — a `^[0-9]+` match reads `2.26432e+07` as `2`, which is how a regression that collapsed a 10000×4669 output to 10×10 once passed as "2 differing pixels". Requires ImageMagick (`compare`, `identify`) for the content-verification steps. Sample `.nc` files in `sample_data/` are git-ignored; fetch them with `reproduction/download_sample.sh` (public NOAA S3, no credentials).

CI (`.github/workflows/ci.yml`) runs the same `tests/run_all_tests.sh` on every push/PR to `main`: installs dependencies via `apt-get`, downloads `sample_data/` (cached by a hash of `download_sample.sh`, so the cache busts automatically if the channel list changes), and lets `run_all_tests.sh` build the project itself.

## Architecture

### Processing Pipeline

Entry point `src/main.c` dispatches to command handlers. Flow:

1. **Parse args** → `src/args.c`: Custom parser supporting subcommands (`gray`, `pseudocolor`, `rgb`)
2. **Load NetCDF** → `src/reader_nc.c`: Reads GOES L1b (Rad) or L2 (CMI/LST/SST/etc.) products
3. **Apply corrections** → Rayleigh (`src/rayleigh.c`), gamma, CLAHE (`src/image.c`)
4. **Normalize to 8-bit** → `src/gray.c` or `src/truecolor.c`
5. **Optional reproject** → `src/reprojection.c`: Fixed grid to geographic (lat/lon equirectangular)
6. **Write output** → `src/writer_png.c` or `src/writer_geotiff.c` (auto-selected by `-o` extension or `-t`), plus optional JSON sidecar via `src/metadata.c`

Cross-cutting: `src/timing.c` accumulates the per-stage times the existing `LOG_TIMING` sites already measure and appends one CSV row per run when `--timing-csv` is given (see Gotchas).

### CLI Invocation & Output

Invocation is `hpsv <gray|pseudocolor|rgb> <anchor.nc> [options]` — the anchor file drives scene/channel inference (see Filename Inference). Flags are registered in `src/main.c` (`ap_add_*`) and consumed into `ProcessConfig` in `src/config.c`. Non-obvious behaviors worth knowing:

- **JSON sidecar is opt-in**, gated on `-j`/`--json` (`save_sidecar_json()` in `src/main.c` early-returns otherwise).
- `-G`/`--geographics` reprojects fixed-grid → lat/lon equirectangular; `-B`/`--both` emits the fixed-grid **and** geographic outputs in a single run.
- `-o` accepts `{...}` filename tokens (`{SAT}`, `{TS}`, `{CH}`, `{PROD}`, etc.) expanded from metadata; with no `-o`, a deterministic name is generated from the anchor.
- **`pseudocolor` without `-p`** uses the internal `rainbow` palette (`create_rainbow_color_array()` in `src/palette.c`, 256 colors, generic blue-to-red) auto-scaled to the data's actual min/max — it does not fall back to plain grayscale.

### Core Types

- `DataF` (`include/datanc.h`): Float grid for satellite radiance/physical data. Allocate with `dataf_create()`, free with `dataf_destroy()`. Use `IS_NONDATA()` macro for fill-value checks.
- `ImageData` (`include/image.h`): 8-bit RGB/grayscale for output. Allocate with `image_create()`.
- `ChannelSet` (`include/channelset.h`): Multi-channel bundle for RGB modes.
- `ProcessConfig` (`include/config.h`): Immutable config struct built from parsed args — always use this, never ad-hoc params. Parsed in `src/config.c`.
- `MetadataContext`: Mutable context built as processing proceeds.

### Filename Inference

Anchor file (`OR_ABI-L1b-RadF-M6C13_G16_s20253231800172...nc`) identifies the scene. `find_id_from_name()` extracts the timestamp signature (`s20253231800`), then sibling channels are located by replacing `C13` → `C01`, `C02`, etc. in the same directory. See `src/channelset.c`.

### RGB Mode System

Modes defined in `src/rgb.c` switch: `truecolor`, `night`, `ash`, `airmass`, `daynite`, `severestorm`, `so2`, `custom`. Each specifies channel combinations and per-channel linear algebra. `daynite` auto-blends day/night using solar geometry from `src/daynight_mask.c`. `night` (and the night side of `daynite`) renders C13 brightness temperature via `create_nocturnal_pseudocolor()` (`src/nocturnal_pseudocolor.c`), optionally composited over a city-lights background read by `src/reader_webp.c`.

True color synthesizes a green channel not present in ABI: `G = 0.465*B + 0.465*R + 0.07*NIR`.

### Rayleigh Correction

Two implementations in `src/rayleigh.c`:
- **LUT** (`--rayleigh`, default): Lookup tables from pyspectral, embedded in binary via `src/rayleigh_lut_embedded.c` (regenerated with `assets/embed_luts.py`). Indexed by `sec(SZA)`, `sec(VZA)`, `180° − Δφ` (azimuth convention matches pyspectral).
- **Analytic** (`--ray-analytic`): Bucholtz (1995) model + Hansen & Travis (1974) phase function. Lighter, less accurate.

Both apply cloud relaxation: correction fades to zero when C02 reflectance exceeds 0.20 (linear rolloff to 1.0).

### Band Algebra (`--expr`)

`src/parse_expr.c` parses linear-combination expressions like `"C13-C14"` or `"2.0*C13-1.0*C15+300"` into a `LinearCombo` (`include/parse_expr.h`): up to 10 `{band_id, coeff}` terms plus a constant bias. This is cross-cutting, not RGB-only:

- `gray`/`pseudocolor` take one expression via `--expr`, consumed in `src/processing.c`.
- `rgb --mode custom --expr "R;G;B"` splits on `;` into three independent combos, consumed by `compose_custom()` in `src/rgb.c`.

`config_from_argparser()` sets `cfg->is_custom_mode`/`cfg->custom_expr` whenever `--expr` is present (`src/config.c`); `get_unique_channels_rgb()` derives which sibling channel files need loading before evaluation.

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
- Inspect GeoTIFF GDAL metadata without GDAL CLI tools: `strings file.tif | grep -A0 'Item name'`

## Gotchas

- **Channel arrays are 1-indexed**: `RgbContext.channels[17]` uses indices 1–16 (C01–C16); index `[0]` is unused. Don't iterate from 0.
- **Don't alias `ctx->comp_{r,g,b}` to a `channels[N].fdata` struct directly** (e.g. `ctx->comp_b = ctx->channels[13].fdata;`): `DataF` copies by value but its heap buffer doesn't, so the alias and the original share one buffer. `rgb_context_destroy()` unconditionally frees both `channels[1..16]` *and* `comp_r/g/b`, so an alias double-frees and segfaults on cleanup — found via `tests/test_rgb.sh`'s `ash` sanity check (`compose_ash`/`compose_so2` in `src/rgb.c` both did this; fixed with `dataf_copy()`, matching the pattern `compose_truecolor` already used). If a composer needs to reuse a channel verbatim as one of the three output planes, copy it.
- **Reprojection gap fill**: corner/out-of-disk cells in the reprojection grid are filled with a nodata pattern (`-a`/`--alpha` → transparent; pseudocolor with a `.cpt` `N` color → that color), not real data. Pseudocolor without `--alpha` and without an `N` entry in the palette logs a `LOG_WARN` since out-of-disk cells can't be distinguished from real data in that case. Covered by `tests/test_reprojection.sh`.
- **Command exit codes**: `args.c`'s `ap_parse()` invokes the active subcommand's callback and stores its return value in `parser->cmd_callback_exit_code`, retrievable via `ap_get_cmd_exit_code()` — but `ap_parse()` itself only returns a `bool` for *argument-parsing* success. `main()` must explicitly call `ap_get_cmd_exit_code()` after `ap_parse()` and return that; returning a hardcoded `0` (the bug prior to this fix) makes every runtime failure (bad file, bad palette, OOM, etc.) silently report success to the shell. Any code path in `run_processing()`/`run_rgb()` that adds a new `goto cleanup` must leave `status` at its non-zero initial value (`1`) on failure — don't reset it to 0 except on the success fallthrough.
- **`run_all_tests.sh` rebuilds the project, and that is how the CUDA suite gets skipped**: it runs `make clean && make ${CUDA:+CUDA=$CUDA}` (`tests/run_all_tests.sh:69`). Invoked plainly it rebuilds **without** CUDA — destroying any `make CUDA=1` binary you had — and `test_cuda.sh` then detects a non-CUDA binary, exits 0 with `SKIP`, and is **counted as passed**. A green 9/9 therefore does not mean the GPU path was tested. Use `CUDA=1 CUDA_ARCH=sm_XX tests/run_all_tests.sh` and confirm with `ldd bin/hpsv | grep cudart`. The build banner now also reports `GPU: CUDA sm_XX` vs `sin CUDA`.
- **The `--timing-csv` stage taxonomy must stay identical in both builds.** `LOG_TIMING_STAGE(TM_*, ...)` (`include/timing.h`) wraps `LOG_TIMING` and additionally accumulates into one of 11 stages; the record exists to compare the OpenMP and CUDA builds column by column, so a stage timed in one build and not the other silently biases that column. This already bit once: the CUDA build timed piecewise stretch and ratio sharpening while the CPU counterparts (`apply_piecewise_stretch()`, `dataf_ratio_sharpen_map()`, plus `image_apply_clahe()`) had no timer at all — they were added. When adding a stage or a kernel, tag the CPU and GPU sites together. Note also that `nav_cuda.cu`'s "Solar+satellite geometry" is the counterpart of the CPU's *two* geometry timers (`TM_GEOM`), not of navigation (`TM_NAV`) — the file's own comment warns about this pairing. Stage times deliberately do not sum to `t_total`; the remainder is untimed orchestration (~14% on an rgb run).
- **`find_id_from_name()` keeps 11 characters, one short of the full minute** (`s2025323180`, not `s20253231800` as its own comment claims). That is enough to find sibling channels of a full disk, but ten consecutive mesoscale scenes share the truncated key. `src/timing.c` extracts its own 12-character signature for the CSV join key rather than reusing it.
- **Help text is compile-time selected**: `HPSV_LANG=es` defines `-DHPSV_LANG_ES`, switching `include/help_en.h` ↔ `include/help_es.h`. Keep both in sync when changing CLI help.
- `.github/copilot-instructions.md` predates some flags (e.g. it lists a `-r` reproject flag and an always-on JSON) — prefer this file and the code when they disagree.
- **Predefined clip CSV path is hardcoded twice, and the override comment is wrong**: both `RUTA_CLIPS` in `src/main.c` (used by `--list-clips`) and a separate literal in `config_parse_clip()` in `src/config.c` (used by `-c <key>`) point at `/usr/local/share/lanot/docs/recortes_coordenadas.csv` (a LANOT/UNAM deployment path). `src/main.c`'s comment claims it's "overridable via a build-time -D macro", but the `#define` has no `#ifndef` guard, so a command-line `-DRUTA_CLIPS=...` just triggers a harmless-looking redefinition warning and is silently discarded (verified by compiling a minimal repro) — the hardcoded path always wins. Changing the clips location means editing both call sites; there's no actual build-time override today.
- **`docs/hpsatviews.schema.json` has drifted from the real JSON sidecar**: it requires `timestamp_iso` and lists `command` enum `["rgb", "gray", "pseudocolor", "composite"]`, but `src/metadata.c` writes the key as `timestamp` (matches the README §5.7 example) and there is no `composite` subcommand (only `gray`/`pseudocolor`/`rgb`). Don't trust the schema file over an actual sample sidecar or `src/metadata.c` when reasoning about JSON output shape.
