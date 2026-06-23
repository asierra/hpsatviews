# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

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

[Unreleased]: https://github.com/asierra/hpsatviews/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/asierra/hpsatviews/releases/tag/v1.0.0
