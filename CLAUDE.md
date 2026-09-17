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

These are end-to-end regression tests: each script runs `hpsv` on `sample_data/` and most also verify output content with `tests/compare_image.sh`, a tolerant pixel diff (ImageMagick `compare -metric AE -fuzz 2%`, default 1% of pixels allowed to differ) against reference PNGs/GeoTIFFs committed under `tests/expected_output/` (excepted from the root `*.png`/`*.tif` gitignore rules via `tests/.gitignore`). For `.tif`/`.tiff` inputs, `compare_image.sh` forces page `[0]` on both operands (COG outputs embed overview pyramids as extra TIFF pages that otherwise confuse `compare`/`identify`) and strips libtiff "Unknown field with tag..." warnings (unrecognized GeoTIFF private tags) before parsing the AE value. `run_all_tests.sh` aggregates per-suite pass/fail counts. Test scripts: `test_rgb.sh` (exact pixel diff only for `truecolor`; `night`/`ash`/`daynite` and both Rayleigh variants get a lightweight `check_nonblank` — ImageMagick `identify -format "%[standard-deviation]"` must be `>0` — since maintaining an exact reference per mode isn't worth it, but a blank/degenerate output should still fail; `airmass`/`severestorm`/`so2` aren't exercised because their channels, C05/C07–C10/C12, aren't in `sample_data/`), `test_pseudo.sh`, `test_clahe.sh`, `test_geotiff.sh` (pixel diff + GDAL metadata via `strings | grep`, since `GDALSetMetadataItem()` embeds metadata as readable XML inside the TIFF — no GDAL CLI tools needed), `test_reprojection.sh` (`-G`; also checks a corner pixel directly to catch the nodata-fill regression — see Gotchas), `test_json.sh` (STAC Item checks: key/value greps, JSON Schema validation against `docs/stac/hpsv-item.schema.json`, and a coherence pass that opens each declared asset and compares `proj:transform`/`proj:shape` against the actual GeoTIFF — needs `python3-jsonschema` and the GDAL Python bindings, and fails loudly if either is missing rather than skipping; `tests/stac_validators.sh` prints the install command for the local package manager, since the project builds on RHEL/Rocky too and an apt-only message sends half the hosts after a package name that does not exist there; covers gray PNG and GeoTIFF, both pseudocolor palettes, `-G`, `-B`, `--cog`, `--stac-collection`, and rgb), `test_sweep.sh` (the retroactive sweep, `tools/stac_sweep.py`: rebuilds an Item from hpsv-written GeoTIFFs and **cross-checks it against the Item hpsv emits for the same files** — see Gotchas), `test_siblings.sh` (sibling-channel lookup against decoy scenes; see Gotchas), `test_config.sh` (parser-only: most cases append `--help`, so it checks flag acceptance, not pipeline behavior), `test_fastread.sh`, `test_cuda.sh` (12 cases comparing every GPU path against its CPU reference; two of them use `cmp` for byte-identity rather than the tolerant diff, because they compare two routes that must produce the *same memory* — the device handoff, and `HPSV_NO_DEVICE_HANDOFF=1`).

`compare_image.sh` also fails when the two images differ in **dimensions** (`compare` crops to the intersection instead of erroring), and parses the AE as a full float including scientific notation — a `^[0-9]+` match reads `2.26432e+07` as `2`, which is how a regression that collapsed a 10000×4669 output to 10×10 once passed as "2 differing pixels". Requires ImageMagick (`compare`, `identify`) for the content-verification steps, and `python3-jsonschema` plus `python3-gdal` for `test_json.sh` (schema validation, and reading the GeoTIFF geotransform back to check the Item describes it). Sample `.nc` files in `sample_data/` are git-ignored; fetch them with `reproduction/download_sample.sh` (public NOAA S3, no credentials).

CI (`.github/workflows/ci.yml`) runs the same `tests/run_all_tests.sh` on every push/PR to `main`: installs dependencies via `apt-get`, downloads `sample_data/` (cached by a hash of `download_sample.sh`, so the cache busts automatically if the channel list changes), and lets `run_all_tests.sh` build the project itself.

## Architecture

### Processing Pipeline

Entry point `src/main.c` dispatches to command handlers. Flow:

1. **Parse args** → `src/args.c`: Custom parser supporting subcommands (`gray`, `pseudocolor`, `rgb`)
2. **Load NetCDF** → `src/reader_nc.c`: Reads GOES L1b (Rad) or L2 (CMI/LST/SST/etc.) products
3. **Apply corrections** → Rayleigh (`src/rayleigh.c`), gamma, CLAHE (`src/image.c`)
4. **Normalize to 8-bit** → `src/gray.c` or `src/truecolor.c`
5. **Optional reproject** → `src/reprojection.c`: Fixed grid to geographic (lat/lon equirectangular)
6. **Write output** → `src/writer_png.c` or `src/writer_geotiff.c` (auto-selected by `-o` extension or `-t`), plus an optional STAC Item via `src/metadata.c`

Cross-cutting: `src/projection.c` is the single place that turns a file's projection attributes into PROJ.4/WKT/an OGR handle (`src/writer_geotiff.c` used to build its own); `src/footprint.c` computes the EPSG:4326 footprint of every output (see Gotchas). `src/timing.c` accumulates the per-stage times the existing `LOG_TIMING` sites already measure and appends one CSV row per run when `--timing-csv` is given (see Gotchas).

### CLI Invocation & Output

Invocation is `hpsv <gray|pseudocolor|rgb> <anchor.nc> [options]` — the anchor file drives scene/channel inference (see Filename Inference). Flags are registered in `src/main.c` (`ap_add_*`) and consumed into `ProcessConfig` in `src/config.c`. Non-obvious behaviors worth knowing:

- **The STAC Item is opt-in**, gated on `-j`/`--json` (`save_stac_item()` in `src/main.c` early-returns otherwise), and `cfg->save_json` also gates the metadata that only exists to be written (footprint, output grid, WKT2). Until v1.1.0 this flag wrote a sidecar of our own design; the Item replaced it in phase 3 of `docs/stac/STAC_PLAN.md`.
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

**Terminator limits live in `include/rayleigh.h`, not inline.** `HPSV_SUNZ_LIMIT` (88°) and `HPSV_SUNZ_MAX_SZA` (95°) are satpy's `SunZenithCorrector` defaults, which is what geo2grid runs with; `apply_solar_zenith_correction()` (`src/truecolor.c`) ports `_sunzen_corr_cos_ndarray` from satpy — `1/cos(SZA)` below the limit, then the gain frozen at its value there and faded logarithmically to zero at 95°. The three things that have to move together: that function, the night mask and taper in `luts_rayleigh_correction()` (a `theta_s >` test that writes `0.0f` — leave it at 88° and the Rayleigh step blacks out the very band the normalization just made visible), and `analytic_rayleigh_correction()`. `HPSV_RAY_LUT_SZA_MAX` (87.68° = `arccos(1/24.75)`) is a different kind of number: the pyspectral table's own ceiling, not a rendering limit, and it does not move with the others. Both CUDA kernels in `src/cuda/rayleigh_cuda.cu` mirror all of this and get the constants from the same header (it is already inside their `extern "C"` block); `inv_cos_limit`/`inv_log2` are computed on the host and passed to the kernel so both paths multiply by the identical float.

**Day/night blending limits are in `include/daynight_mask.h`** (`HPSV_DN_TERMINATOR` 85°, `HPSV_DN_PENUMBRA` 10°, i.e. blend 75–85), shared with `src/cuda/daynite_cuda.cu` through `cuda_daynite.h`. The mask interpolates on `sin(elevation)`, which is identically `cos(SZA)`, the same variable satpy's `DayNightCompositor` uses, so aligning the two would be **only the constants** — and that was tried, measured and deliberately not taken. satpy blends 85–88; our 75–85 is wider and starts earlier. Narrowing it replaces the nocturnal IR composite with true colour over a ten-degree band and takes the cloud-top temperature coding with it: on a GOES-19 full disk at dusk a whole frontal system went from a blue-to-yellow thermal structure to a featureless white mass. `--cloud-temp` does not fix that — it is a global threshold, so `-T 250` restores the structure at dusk but scatters IR patches across the entire sunlit disk (19.65 % of the disk differs from the 75–85 rendering, against 1.42 % for the limits we kept). It is a forecasting trade, not a correctness one. `HPSV_DN_TERMINATOR` must not *exceed* `HPSV_SUNZ_LIMIT`, or the blend pulls in a day side that `apply_solar_zenith_correction()` has already faded and a seam appears — so raising it to 88 is available, lowering `HPSV_SUNZ_LIMIT` is not.

**`--ir-overlay` did not make satpy's blend adoptable either.** `image_overlay_ir()` (`src/nocturnal_pseudocolor.c`, mirrored by `image_overlay_ir_dev()` in `src/cuda/daynite_cuda.cu`) paints the IR palette over the day composite with a ramp between `--ir-range` T1,T2 (default 220,240 K). It runs *after* the mask is computed and *before* the blend, in both paths, and skips pixels the mask marks as full night. The defaults were swept on GOES-19 full disks: with T2 ≥ 250 K Antarctic sea ice and mid-level daytime cloud get tinted, and with T2 cold enough to avoid that the overlay no longer rescues the 75–85° band under an 85–88° blend — so the blend stayed put and the overlay is a day-side enhancement only. The palette lookup is `atmosrainbow_index()` (`include/palette.h`), a binary search with the same edge semantics as the linear scan in `create_nocturnal_pseudocolor()`; the thresholds are *not* evenly spaced (0.81–3 K steps), so a direct index would pick different entries. In the CUDA path the host copy of the day image was downloaded before the overlay, so the no-blend branch of `compose_daynite_cuda()` has to download it again. To revisit the thresholds or the blend limits, rerun the study rather than eyeballing one scene: `reproduction/pick_scenes.sh` picks complete scenes (full days live under `/depot/goes-east/l1b/abi/fd/<year>/<doy>` at LANOT; `/data1` keeps only fragments), and `reproduction/sweep_ir_overlay.sh` renders every `--ir-range` pair and builds side-by-side montages at native resolution over the regions that decided it (Weddell sea ice, marine stratocumulus, the Altiplano). `HPSV_BASE_BIN` lets the reference panel come from a different build, which is how a blend-limit change was isolated from the thresholds.

### Band Algebra (`--expr`)

`src/parse_expr.c` parses linear-combination expressions like `"C13-C14"` or `"2.0*C13-1.0*C15+300"` into a `LinearCombo` (`include/parse_expr.h`): up to 10 `{band_id, coeff}` terms plus a constant bias. This is cross-cutting, not RGB-only:

- `gray`/`pseudocolor` take one expression via `--expr`, consumed in `src/processing.c`.
- `rgb --mode custom --expr "R;G;B"` splits on `;` into three independent combos, consumed by `compose_custom()` in `src/rgb.c`.

`config_from_argparser()` sets `cfg->is_custom_mode`/`cfg->custom_expr` whenever `--expr` is present (`src/config.c`); `get_unique_channels_rgb()` derives which sibling channel files need loading before evaluation.

## Conventions

- **Language**: C11 with POSIX extensions (`-std=c11 -D_POSIX_C_SOURCE=200809L`)
- **Naming**: `snake_case` for functions, `PascalCase` for types
- **Error handling**: Return `int` (0=success, non-zero=error) or NULL pointer on failure
- **Logging**: `LOG_ERROR()`, `LOG_WARN()`, `LOG_INFO()` from `include/logger.h`. The default level is **`LOG_INFO`, so `LOG_INFO` prints without `-v`** — what `-v` does is lower the threshold to `LOG_DEBUG` (`logger_init(verbose_mode ? LOG_DEBUG : LOG_INFO)` in `src/main.c`), which is where `LOG_TIMING`/`LOG_TIMING_STAGE` live, and that is why the per-stage times only show with `-v`. A `make DEBUG=1` build defines `DEBUG_MODE` and starts at `LOG_DEBUG` regardless. Put anything that would be noise in a cron run behind `LOG_DEBUG`, not `LOG_INFO`: every message carries a timestamp and `file:line`, and production runs these commands unattended.
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
- **`run_all_tests.sh` rebuilds the project, and that is how the CUDA suite gets skipped**: it runs `make clean && make ${CUDA:+CUDA=$CUDA}`. Invoked plainly it rebuilds **without** CUDA — destroying any `make CUDA=1` binary you had — and `test_cuda.sh` then prints `SKIP:` and exits 0. Since 1.2.0 the summary reports it as `Suites saltadas: 1` instead of counting it as passed, and with `CUDA=1` in the environment a skip is a failure: that is what caught a driver/library mismatch on tahan, where `nvidia-smi -L` stopped answering and the suite used to go green without running. Use `CUDA=1 CUDA_ARCH=sm_XX tests/run_all_tests.sh` and confirm with `ldd bin/hpsv | grep cudart`; the build banner reports `GPU: CUDA sm_XX` vs `sin CUDA`. A new suite that can skip should print a line starting with `SKIP:`, which is what `run_test_suite()` looks for.
- **The `--timing-csv` stage taxonomy must stay identical in both builds.** `LOG_TIMING_STAGE(TM_*, ...)` (`include/timing.h`) wraps `LOG_TIMING` and additionally accumulates into one of 11 stages; the record exists to compare the OpenMP and CUDA builds column by column, so a stage timed in one build and not the other silently biases that column. This already bit once: the CUDA build timed piecewise stretch and ratio sharpening while the CPU counterparts (`apply_piecewise_stretch()`, `dataf_ratio_sharpen_map()`, plus `image_apply_clahe()`) had no timer at all — they were added. When adding a stage or a kernel, tag the CPU and GPU sites together. Note also that `nav_cuda.cu`'s "Solar+satellite geometry" is the counterpart of the CPU's *two* geometry timers (`TM_GEOM`), not of navigation (`TM_NAV`) — the file's own comment warns about this pairing. Stage times cover ~97% of `t_total` (they did not until `TM_INIT`/`TM_OPEN`/`TM_UNPACK` were added: the calibration loop in `load_nc_sf()`, which does the Planck inversion, and CUDA context creation — 0.24 s, a quarter of a GPU run — were both invisible).
- **Adding or reordering a `--timing-csv` column means bumping `TIMING_SCHEMA`** (`src/timing.c`). A row is only ever appended to a file whose first line reads `#hpsv-timing schema=<current>`; under any other schema `effective_path()` diverts the rows to `<name>.schema<N>.csv` and logs a `LOG_WARN`. This exists because it already happened: a binary with 15 stages appended 60-field rows to a file whose header had 58 columns, silently shifting every column after the new one (a `load1` of 11.47 read as 31). The header is generated from `kStageName[]` via `header_columns()`, so the enum in `include/timing.h` is the single source of truth — keep the two in the same order.
- **Sibling channels must match the anchor's whole leading name, down to the minute.** `channelset_set_anchor()` records product, sector, scan mode, satellite and `sYYYYJJJHHMM`; `find_channel_filenames()` accepts only names that share all of them, and if two still match one channel it keeps the one with the anchor's exact start token. Until 1.2.0 the key was cut at the tens of minutes and sector/satellite were not checked, so in a mesoscale directory (one scene per minute) an anchor loaded its channels — itself included — from whichever of up to ten scenes `readdir()` listed last. Measured over whole days of full disk, CONUS and mesoscale in `/depot`, every channel of a scene carries the identical start token, so the minute is a safe key. `tests/test_siblings.sh` builds decoy scenes (next minute, same minute with other seconds, other sector, other satellite) and fails on the old code. `src/timing.c` still extracts its own 12-character signature for the CSV join key.
- **The geographic footprint follows the limb, and it deliberately does not use PROJ.** `src/footprint.c` walks the raster border and, where a sample falls off the visible disk, bisects it towards the centre until it lands back on Earth; where an edge actually crosses the limb it also bisects *along the edge* to keep that corner. Both matter: a full disk's four corners are all off the planet (its footprint is the limb ellipse, bbox `lon_0 ± 81.3°`), and a CONUS sector's north-west corner is off it too — skipping the edge crossing cost half a degree of latitude. The inverse is the same closed form `compute_navigation_nc()` runs per pixel (`src/reader_nc.c:537-570`), *not* GDAL's `OCTTransform`: the two agree to 3e-06° (36 cm), but building the PROJ pipeline costs ~17 ms, a tenth of a small run, against under a millisecond for the analytic version. If you ever swap it back, measure that first.
- **The output geotransform lives in `projection_output_geotransform()`, not inlined per writer.** The crop shift uses the *original* pixel size and must precede the resampling stretch; swapping them misplaces the origin by the scale factor. It used to be copy-pasted inside each `is_geotiff` branch (`processing.c` twice, `rgb.c` once), which is why a PNG had no transform at all and why the metres extent disagreed with the GeoTIFF under `-s`. `projection_stac_transform()` wraps it for metadata, converting the fixed grid's scan-angle radians to metres and reordering to STAC's `proj:transform` convention — **which is not GDAL's**: `[pixel_w, row_rot, origin_x, col_rot, pixel_h, origin_y]` against GDAL's `[origin_x, pixel_w, row_rot, origin_y, col_rot, pixel_h]`.
- **In `rgb` the output grid is recorded after `apply_scaling()`, on purpose.** The geometry block that sets `crs`/`bounds` runs *before* the resampling, so `ctx.final_image` there still has the pre-scale size; recording `proj_shape` from it would describe an image that was never written. `processing.c` does not have this problem because its fixed-grid block already works on the resampled `fg_final`.
- **A footprint bbox is never widened to the whole planet to hide an antimeridian crossing.** Longitudes are accumulated as offsets from the subsatellite meridian, not as raw values: a GOES-West full disk spans −218°…−56°, whose naive min/max is `[-179, +176]`, i.e. everything. `ring_bbox()` folds the wrap into the STAC convention instead, west > east, and sets `crosses_antimeridian`. Any consumer comparing `bbox[0] < bbox[2]` has to handle that case.
- **Help text is compile-time selected**: `HPSV_LANG=es` defines `-DHPSV_LANG_ES`, switching `include/help_en.h` ↔ `include/help_es.h`. Keep both in sync when changing CLI help.
- **The clip catalog path is `RUTA_CLIPS` in `include/clip_loader.h`**, used by both `--list-clips` and `-c <key>`. It defaults to the LANOT deployment path `/usr/local/share/lanot/docs/recortes_coordenadas.csv` and is `#ifndef`-guarded, so another site overrides it at build time with `make CFLAGS_EXTRA='-DRUTA_CLIPS=\"/path/clips.csv\"'`. Until 1.2.0 the path was a literal in two places and the `-D` override was silently discarded.
- **`eo:bands` hangs off the asset, and only when the asset *is* that band.** eo v1.1.0 requires it on the asset — an Item carrying it only in `properties` does not validate, measured against the official schema — and a composite declares none at all, nor the eo extension, because three derived planes are not the four ABI channels they came from. Same principle as `hpsv:channels` vs `raster:bands`. `emit_eo` in `metadata_save_stac_item()` is the single gate for both the extension list and the asset field; keep them together.
- **The footprint algorithm exists twice, and one test is what keeps the copies together.** `src/footprint.c` walks the raster border in C; `tools/stac_sweep.py` ports the same walk to Python, because the sweep reads GeoTIFFs and never touches the C. Both start at the same corner and go the same way **on purpose**, so `tests/test_sweep.sh` can compare the two rings *vertex by vertex* against the same input and catch a divergence. Residual agreement is ~1.6e-05° (about 2 m), from the C reading the ellipsoid out of the NetCDF while Python reads the GeoTIFF's CRS. The Python side projects with GDAL's `osr`, not `pyproj`, although pyproj's API is nicer: GDAL is already a hard dependency of the script and pyproj is not packaged for EPEL 10, which is what one of the production servers runs. Both call the same PROJ, and the swap was verified to be exact — the two rings came out bit-identical, and the residual against the C stayed at 1.588e-05°. If you change the walk, change both and rerun that suite — nothing else would notice.
- **The sweep cannot recover `hpsv:channels` or `hpsv:enhancements`**, because they were never written inside the GeoTIFF; reconstructed Items declare `hpsv:reconstructed: true` and simply omit them rather than invent values. A GeoTIFF whose `tool` tag is not `hpsatviews` is skipped, not half-reconstructed. `gray`/`pseudocolor`/`rgb` are told apart by raster shape (one band without a colour table, one band with it, three bands), since no tag records the subcommand.
- **The official STAC schemas are vendored under `docs/stac/schemas/`**, fetched by `tools/fetch_stac_schemas.py`, which reads the declared versions out of `src/metadata.c` rather than repeating them. `tests/test_json.sh` validates every Item against the core plus each declared extension, offline. To move a version: edit the `#define`s in `metadata.c`, rerun the script, review the diff. Downloading at test time was rejected on purpose — the suite would depend on a third-party service being up, and the pinned versions would stop being visible in the tree.
- **The id's product segment is the `-N` short label, not the mode, and it is sanitized.** Production builds several `--mode custom` products per scene and tells them apart *only* by that label, so forcing the mode into the id would collapse them all onto one identifier. But a label is human text: `-N "Ceniza Volcanica"` used to produce the id `hpsv_..._Ceniza Volcanica`, with a space, in a public contract that also becomes a filename. `build_stem()` now replaces anything outside `[A-Za-z0-9._-]`. Note `-N "A:B"` puts **A** (before the colon) in the id via `product_short`, and B is the descriptive name — the opposite of what the ordering suggests.
- **`hpsv:enhancements.mode` is the real mode; the label lives in `hpsv:product`.** They used to be the same field, so `-N` erased the mode from the record. And for `--mode custom` the `--expr` *is* how the bands were combined: it was only recorded from `processing.c`, never from `rgb.c`, so a bespoke composite documented nothing about itself. `--minmax` was recorded nowhere at all, though production passes it, sometimes deliberately reversed. All three are in the Item now.
- **The Item id is not `metadata_build_filename()`.** That builder encodes gamma, CLAHE and clipping in the name (`build_ops_string()`), so using it as an identifier would make two renderings of one scene into two items. `metadata_build_id()` shares the same stem — `hpsv_<SAT>[_<SECTOR>]_<YYYYJJJ_hhmm>_<TYPE>[_<BANDS>]` — and stops before the operations. The Item file is named after the id, **not** after `-o`, so two runs over one scene converge on one file; only the directory comes from `-o`. Asset paths never go through `metadata_add_str()`, which truncates at 63 characters.
- **`docs/stac/hpsv-item.schema.json` describes what the emitter actually writes, and `tests/test_json.sh` validates it on every run.** It is not a substitute for the official STAC schemas — validating against the core and extension schemas is phase 5 of the plan — but the direction of authority is the same as before: **the schema follows `src/metadata.c`, not the other way round**. If you add a key, add it to the schema in the same commit or the suite fails. The spec and extension versions are `#define`d together at the top of `metadata_save_stac_item()` precisely so revalidating them is one edit.
