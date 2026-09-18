# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.2.0] - 2026-09-16

STAC Items, a solar terminator aligned with satpy, a day-side IR enhancement
for `daynite`, and a fix to how sibling channels are found that matters for
mesoscale.

**Upgrading:** `-j` writes a STAC Item instead of the previous sidecar, named
after the Item id rather than after `-o` (see below); anything that read the
sidecar has to read the Item. From a release older than 1.1.0 there is a
second change: `daynite` no longer draws city lights unless `-l` is passed (a
1.1.0 change its notes did not mention). LANOT's full-disk and CONUS scripts
lost their lights on the first run after the upgrade and now pass `-l`.

### Added
- STAC Items. `-j` writes a STAC 1.0.0 Item (projection, processing and, for
  single-band outputs, eo extensions) instead of the sidecar of our own design,
  in the output directory and named after the Item id — the scene plus the
  product, without the enhancement segment — so two renderings of one scene
  converge on one file. Each asset carries its own `proj:transform`,
  `proj:shape` and CRS, which `-B` needs: the fixed-grid and geographic files
  live on different grids. `--stac-collection <id>` sets the collection. The
  footprint is the EPSG:4326 outline of the data, following the limb on a full
  disk and folding the antimeridian into STAC's west > east convention for
  GOES-West. `hpsv:enhancements` records the real mode, `--expr` for
  `--mode custom`, and `--minmax`; the id's product segment is sanitized to
  `[A-Za-z0-9._-]`. The suite validates every Item against the vendored
  official schemas and checks it against the GeoTIFF it describes.
- `tools/stac_sweep.py`: rebuilds Items for GeoTIFFs hpsv wrote before Items
  existed. What was never stored in the file (`hpsv:channels`,
  `hpsv:enhancements`) is omitted and the Item says `hpsv:reconstructed`.
- `--timing-csv <file>`: one CSV row per run with per-stage times, the scene,
  end-to-end latency and host context, identical in the OpenMP and CUDA builds.
  Concurrent runs can share a file; a file written under another column schema
  is never appended to.
 (daynite): overlays the IR palette on the day side, so cold
  cloud tops show over true colour instead of the day/night mask swapping one
  composite for the other. The weight is a linear ramp set by
  `--ir-range T1,T2` — pure IR at or below `T1`, true colour untouched at or
  above `T2` — defaulting to 220,240 K. Opt-in; the operational rendering does
  not change unless the flag is passed. Both thresholds are recorded in the
  STAC Item's `hpsv:enhancements` (`ir_overlay_opaque_k`, `ir_overlay_clear_k`),
  and the CUDA build runs it on the device-resident composite.

  The defaults come from sweeping ten GOES-19 full disks (days 245 and 255 of
  2026, 12–22 UTC) at native resolution. Marine stratocumulus off Peru and
  Chile — the case a warm `T2` was expected to break — stays untouched at every
  pair tried. What breaks first is cold *surface*: with `T2` at 250 K or above,
  Antarctic sea ice in the Weddell Sea turns cyan, and wide areas of mid-level
  daytime cloud turn blue. 225,245 already lays a lavender veil over mid-level
  cloud; 220,240 leaves both alone while deep convection reads the same as with
  any of the warmer pairs.

  This was first explored as the way to adopt satpy's 85–88° day/night blend
  without losing the thermal coding at dusk. It is not: with thresholds cold
  enough to spare ice and mid-level cloud, the overlay only covers the deepest
  tops, and the 75–85° band still turns into dark twilight true colour. The
  blend therefore stays at 75–85°, and the overlay is a day-side enhancement
  on top of it.

- `reproduction/sweep_ir_overlay.sh`, `ir_overlay_montage.py` and
  `pick_scenes.sh`: the study behind the `--ir-overlay` defaults, to be rerun
  before changing them or the blend limits.
- `reproduction/float_nav_error.c` and `float_geom_error.c`: what computing
  the navigation and the viewing geometry in single precision costs, over a
  whole GOES-19 disk at 0.5 km, against the double-precision code.

### Changed
- The solar terminator is now handled the way satpy, and therefore geo2grid,
  handles it, in the modes where that is a correctness question.

  `apply_solar_zenith_correction()` used to zero every pixel past 85° of solar
  zenith, leaving a hard black wall across the image; it now ports satpy's
  `_sunzen_corr_cos_ndarray` (the `sunz_corrected` modifier geo2grid's true
  colour uses): `1/cos(SZA)` below 88°, then the gain frozen at its value there
  and faded out logarithmically, reaching zero at 95°. Capping is the part that
  matters — plain `1/cos` is 28.65 at 88° and diverges at 90°. The night mask
  and the taper in the Rayleigh correction move to the same 95° limit, since
  the Rayleigh step wrote a hard zero above 88° and would otherwise black out
  the band the normalization just recovered. `--ray-analytic`, which had no
  taper at all, gets the same one. On the GOES-16 CONUS sample scene
  (day 220/2024, 13:00 UTC, terminator over the Pacific coast) `truecolor
  --rayleigh` goes from 17.06 % pure black to 6.94 %: 10.12 % of the scene is
  now twilight with visible cloud structure instead of a straight cut.

  `daynite`'s blend limits are **not** moved to satpy's, and the operational
  rendering is left exactly as it was. Aligning them too is a two-constant
  change and costs nothing, but on a GOES-19 full disk at dusk it replaces the
  nocturnal composite with true colour across a ten-degree band and takes the
  cloud-top temperature coding with it — a whole frontal system went from a
  blue-to-yellow thermal structure to a featureless white mass. `--cloud-temp`
  cannot stand in for it: it is a global threshold, so `-T 250` restores the
  structure at dusk while scattering IR patches over the entire sunlit disk
  (19.65 % of the disk then differs from the current rendering, against 1.42 %,
  mean 0.66 DN, for what shipped). Whether to make that trade is a forecasting
  decision rather than a correctness one; `include/daynight_mask.h` records what
  was measured, and the change is two constants away if it is ever wanted.

  Rendering the twilight band costs very little compute and a little more I/O.
  Seven interleaved runs of `truecolor --rayleigh --sharpen --stretch` over the
  CONUS sample scene at 5000x3000, where the terminator crosses the image and
  the band is ~10 % of it: the correction stage goes from 0.238 s to 0.254 s
  and enhancement from 0.116 s to 0.123 s, so 0.023 s of actual work on a
  2.8 s run, under 1 %. The visible part of the +0.226 s total is `t_write`,
  0.157 s, which is deflate on a PNG that grew from 20.4 MB to 23.0 MB because
  there is 10 % more image in it. The night side stays as cheap as before:
  everything past 95° still leaves the loop before any transcendental.

  On what production actually runs — a GOES-19 full disk through
  `hpsv rgb -o out.tif -B`, that is `daynite` at 2 km writing two GeoTIFFs —
  five interleaved runs put the total at 8.206 s before and 8.044 s after,
  every stage delta inside the run-to-run range: no measurable cost at all. The
  CONUS PNG figures above are the worst case, a small scene the terminator
  crosses end to end.

  This is what `compare_g2g_product.sh`'s `SZA_MAX` was working around: the
  85–90° band was 2.2 % of a full disk but carried 48 % of the difference
  against geo2grid, plus a red bias that was ours alone. With the wall gone
  that cut no longer corresponds to anything in the code, so its default moves
  from 85° to 95°, `HPSV_SUNZ_MAX_SZA`, where both tools have faded to zero —
  the two now blank in the same place and the statistics cover the whole
  illuminated disk. Pass `SZA_MAX=85` to reproduce figures published under the
  old behaviour. The limits themselves now live in `include/rayleigh.h` and
  `include/daynight_mask.h`, shared with the CUDA kernels so the two paths
  cannot drift.

- The viewing geometry runs in single precision on the GPU, in one kernel
  instead of three. It was the most FP64-bound stage, which is what decided
  whether a card pays: on an RTX 5060 Ti it goes from 0.31 s to 0.006 s on a
  1 km full disk, and from 0.102 s to 0.022 s at 0.5 km on the A30. Measured
  with `reproduction/float_geom_error.c`, single precision moves the solar zenith
  gain by at most 0.017 counts anywhere on the disk; the one trap is the
  scene's hour-angle base (about −7.7·10⁴ rad), which the host now reduces
  modulo 2π before handing it over, or the longitude is lost to rounding.
  Fusing the kernels also drops the solar and satellite azimuth grids, 3.8 GB
  at 0.5 km, so a 0.5 km true colour now fits a 16 GB card: on the RTX 5060 Ti
  it went from falling back to the CPU (47.9 s) to running on the device
  (16.5 s). The CPU path keeps double precision; on the A30 the two differ by
  one count in 1.6·10⁻⁵ of the samples, and in none by more.
- `--timing-csv` records `path=mixed` when the composite ran on the CPU but
  the viewing geometry or the reprojection ran on the GPU, and the `--cuda`
  warning says that instead of "using CPU path": an `airmass -B` gains 37 %
  from the GPU reprojection alone, and neither message showed it.
  `timebudget_summary.py` flags `mixed` groups.
- `tests/run_all_tests.sh` reports a suite that skipped as skipped instead of
  counting it as passed, and with `CUDA=1` in the environment a skipped CUDA
  suite fails. A driver/library mismatch on the GPU server had left
  `nvidia-smi` unable to answer, and the suite used to go green without
  running.
- The clip catalog path is defined once, as `RUTA_CLIPS` in
  `include/clip_loader.h`, and can be overridden at build time with
  `make CFLAGS_EXTRA='-DRUTA_CLIPS=\"..\"'`. The override the source
  advertised was silently discarded before.

### Removed
- `.github/copilot-instructions.md`, stale and unused.

### Fixed
- A seam along the terminator. The solar zenith carried an atmospheric
  refraction term applied only above the horizon, so the zenith jumped by 0.5°
  exactly there, worth 11 % of the faded gain; it was invisible while
  everything past 85° was black. The zenith is now geometric, as in pyorbital,
  which satpy and geo2grid use; the agreement with geo2grid moves by under
  0.1 counts over the whole disk (3.32 to 3.39 mean absolute difference).
- `--cuda` producing a wrong image when the GPU ran out of memory. The CPU
  fallback recomputed the navigation from C01's 1 km grid instead of the
  reference channel's, so at `-f` (0.5 km) the Rayleigh correction read
  geometry for the wrong pixels: 1 % of the samples, by up to 255 counts, on a
  GOES-19 full disk on a 16 GB card. The fallback now matches the CPU path
  exactly.
- `--timing-csv` recorded `path=gpu` whenever `--cuda` was passed, including
  runs that fell back to the CPU. The column now reports the path the image
  was actually produced on.
- Sibling channels loaded from the wrong scene. The key used to find them was
  meant to be the eleven digits of `YYYYJJJHHMM`, but it was copied with its
  leading `s`, so the last minute digit fell off and the key stopped at the
  tens of minutes; sector and satellite were not checked either, and the last
  match `readdir()` returned won. Any directory holding two scenes in the same
  ten minutes was exposed:
  - Mesoscale, one scene per minute: an anchor could load its channels —
    itself included — from any of up to ten scenes.
  - CONUS, every five minutes, always has two scenes per ten minutes. Over
    the 153 such pairs in LANOT's input directory, the pre-fix binary loaded
    all four channels of the other scene for every `x1` anchor (20 of 40
    tried). In real time the `x1` scene is rendered before its `x6` partner
    arrives, so the damage there was rare — by that directory's listing order
    an `x6` anchor would have picked up `x1` in 2 of 153 pairs — but any
    reprocessing after both arrived mixed them.
  - Full disk, one scene per ten minutes in its own directory, was not
    affected.

  The key now reaches the minute and a sibling must share product, sector,
  scan mode and satellite; if two files still match, the one with the anchor's
  exact start wins. Over a whole day of full disk, CONUS and mesoscale every
  channel of a scene carries the identical start, so the minute is a safe
  key. `tests/test_siblings.sh` covers it with decoy scenes.
- `rgb -B -s` crashed: the fixed-grid output scaled the composite in place and
  the reprojection then read the smaller image with the full-size geotransform.
  `-s` now applies to both outputs.
- `rgb` metadata had no `command`, so automatic names used the literal
  `output` as the product type.
- Fill values leaking into real data at the edge of the disk.
  `upsample_bilinear()` and `downsample_boxfilter()` mixed the 1e32 sentinel
  with neighbouring reflectances, leaving limb pixels with values on both sides
  of the `IS_NONDATA()` threshold (1e30); they now return `NonData` whenever an
  input they combine is fill. The `DataF` arithmetic helpers, and a few other
  checks, compared against the exact sentinel instead of using `IS_NONDATA()`,
  so the CPU ratio sharpening could scale such a value back below the threshold
  and render it as saturated data where the GPU path, which uses the threshold,
  left it masked. On a GOES-19 full disk at 0.5 km (`--rayleigh -f --sharpen
  --stretch`) the CPU path had 140 isolated blue samples at 255 on the limb
  where the GPU path had 0, 501 without `--rayleigh`; the two paths now differ
  only by one-count rounding.

## [1.1.0] - 2026-08-11

DOI: [10.5281/zenodo.21893553](https://doi.org/10.5281/zenodo.21893553).

*Note added in 1.2.0:* this release also made city lights optional in
`daynite` (`f0d4130`): the night side shows them only with `-l`. These notes
did not say so, and an installation upgrading from 1.0.x loses its lights.

Optional CUDA backend and a rewritten I/O path. On the production server
(NVIDIA A30, 64 threads) a full-disk GOES-19 true colour with Rayleigh
correction and ratio sharpening renders in 9.25 s on the CPU path and 3.02 s on
the GPU, against 30.63 s for geo2grid 1.3 on the same host and scene. The
like-for-like figure is the CPU one, 3.31×; geo2grid has no GPU path to compare
against.

### Added
- An optional CUDA backend, opt-in at build time (`make CUDA=1
  CUDA_ARCH=sm_XX`) and at run time (`--cuda`). The design is device-resident:
  each channel is uploaded once and the whole composition — navigation grid,
  view geometry, Rayleigh LUT correction, synthetic green, piecewise stretch,
  RGB composition, and the reprojection that consumes the result — is chained
  on the GPU without intermediate round trips. Options with no kernel fall back
  to the CPU transparently, and the OpenMP path remains the reference
  implementation.
- A CUDA kernel for ratio sharpening (`apply_ratio_sharpen_dev`). It fuses the
  three CPU passes — 2×2 block mean, ratio map, and the two multiplications into
  green and blue — into one kernel that recomputes each block mean in place,
  allocating nothing. This also removes `--sharpen` from the list of options
  that disqualify true colour from the accelerated path: until now the flag
  silently sent the whole composite back to the CPU, which mattered because
  sharpening is exactly what is needed to match geo2grid's product, so every
  cross-tool GPU measurement had really been measuring OpenMP. On the A30
  production host a sharpened full disk went from 6.96 s to 3.02 s.
- `reproduction/bench_geo2grid.sh` and `reproduction/compare_g2g_product.sh`:
  a cross-tool benchmark against geo2grid (SSEC/CIMSS) and a check that both
  tools produce the same product. The benchmark sweeps geo2grid's
  `--num-workers` and reports its best time, because its default of 4 workers
  understates it badly on a many-core host, and it runs hpsv with
  `-f --sharpen --stretch` so both tools emit the same 0.5 km sharpened
  composite.
- `AOD` recognized as an L2 product variable in `src/reader_nc.c`.
- CUDA coverage for the `daynite` composite: nocturnal pseudocolour, day/night
  mask and blend now run on the GPU (`src/cuda/daynite_cuda.cu`), together with
  the piecewise stretch and the lat/lon navigation grid. A full-disk
  `daynite -G` render no longer moves intermediate results between host and
  device.
- Run-time escape hatches to disable each optimization without rebuilding, so a
  new host can be evaluated directly: `HPSV_NO_PINNED_UPLOAD`,
  `HPSV_NO_DEVICE_HANDOFF`, `HPSV_NO_PREAD`, `HPSV_NO_MEM_ZEROCOPY` (documented
  in README §6.6, alongside the existing `HPSV_DISABLE_FAST_READ`).
- The build banner now reports whether CUDA support was compiled in, since
  switching modes requires `make clean` and the previous banner was identical
  either way.

### Fixed
- `create_nocturnal_pseudocolor()` left pixels outside the satellite disk
  uninitialized: the writes sat inside the `IS_NONDATA` guard, so those pixels
  kept heap garbage from `malloc`, which `daynite` then blended into the output.
  Results depended on the process's allocation history and were not
  reproducible run to run.
- `tests/compare_image.sh` parsed ImageMagick's pixel-difference count with a
  leading-digits match, so a value in scientific notation (`2.26432e+07`) was
  read as `2` and a 22-million-pixel regression passed as trivial. It also never
  compared image dimensions. Both fixed; the comparator now fails on either.
- `rgb -m truecolor -G --cuda` without `--rayleigh` collapsed the reprojected
  output to 10×10 pixels. Navigation was deferred whenever the CUDA true-colour
  composer was eligible, but that composer only computes it inside the Rayleigh
  block, so without `--rayleigh` no one produced it and the reprojection extent
  stayed at zero. Navigation is now deferred only when the composer will
  actually produce it.
- The satellite geometry log reported `perspective_point_height` in kilometres
  when the value is in metres.

### Changed
- NetCDF reading walks the chunk index once with `H5Dchunk_iter` (HDF5 ≥ 1.14)
  instead of one lookup per chunk, whose per-call cost made the total grow
  quadratically with the chunk count, and then reads the chunk bytes with
  parallel `pread`. Older HDF5 keeps the previous path.
- GeoTIFF writing wraps the existing interleaved pixel buffer in the in-memory
  GDAL dataset instead of copying it into per-band planes.
- H2D transfers pin the host buffer with `cudaHostRegister` when it pays off;
  see README §6.6 for why this is host-dependent.
- README §6.5/§6.6 (en/es) rewritten with current measurements, numerical
  equivalence data, and the caveat that these figures do not transfer between
  GPUs.

## [1.0.1] - 2026-06-30

Bug-fix release. DOI: [10.5281/zenodo.21092353](https://doi.org/10.5281/zenodo.21092353).

### Fixed
- `--full-res`/`-f` was only registered and honored for the `rgb` subcommand;
  `gray`/`pseudocolor` with a multi-channel `--expr` silently ignored it.
  The flag is now parsed once and applies to all three subcommands.

### Changed
- Translated remaining Spanish log messages and internal comments to English
  across the codebase for consistency.

## [1.0.0] - 2026-06-23

Initial public release. DOI: [10.5281/zenodo.20817974](https://doi.org/10.5281/zenodo.20817974).

### Added
- CLI with `gray`, `pseudocolor`, and `rgb` subcommands for GOES-R ABI L1b/L2
  NetCDF products, with automatic sibling-channel inference from a single
  anchor file.
- RGB composite modes: `truecolor`, `night`, `ash`, `airmass`, `daynite`
  (automatic day/night blending via solar geometry), `severestorm`, `so2`,
  and `custom` band algebra expressions.
- Rayleigh atmospheric correction, both LUT-based (pyspectral tables embedded
  in the binary) and a lighter analytic variant (Bucholtz 1995 / Hansen &
  Travis 1974), with cloud relaxation.
- CLAHE contrast enhancement, piecewise contrast stretch, and ratio
  sharpening (geo2grid/satpy `SelfSharpenedRGB`-equivalent).
- Fixed-grid to geographic (lat/lon equirectangular) reprojection, optional
  simultaneous dual output (`-B`), and geographic clipping (`--clip`).
- PNG and Cloud-Optimized GeoTIFF (COG) output, with GDAL-embedded
  georeferencing and colormap metadata for pseudocolor products.
- Optional JSON metadata sidecar (`-j`) and `{...}` filename templating
  (`{SAT}`, `{TS}`, `{CH}`, `{PROD}`, etc.).
- OpenMP-parallelized processing pipeline.
- Bilingual (English/Spanish) CLI help, man pages, and documentation.
- End-to-end regression test suite and GitHub Actions CI.

[Unreleased]: https://github.com/asierra/hpsatviews/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/asierra/hpsatviews/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/asierra/hpsatviews/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/asierra/hpsatviews/releases/tag/v1.0.0
