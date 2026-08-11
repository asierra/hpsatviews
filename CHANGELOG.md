# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
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

[Unreleased]: https://github.com/asierra/hpsatviews/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/asierra/hpsatviews/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/asierra/hpsatviews/releases/tag/v1.0.0
