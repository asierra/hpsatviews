# High Performance Satellite Views (HPSATVIEWS)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C11](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11)
[![CI](https://github.com/asierra/hpsatviews/actions/workflows/ci.yml/badge.svg)](https://github.com/asierra/hpsatviews/actions/workflows/ci.yml)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.20817974.svg)](https://doi.org/10.5281/zenodo.20817974)

Languages: **English** | [Español](README.es.md)

**HPSATVIEWS - High-performance visualization of satellite data**

## 1. Introduction

### 1.1 Summary

**HPSATVIEWS** is a high-performance system for generating **views and
visual products** from environmental satellite data. It generates
grayscale, pseudocolor, and RGB composite views in a matter of seconds,
while preserving geometric rigor and reproducibility. It is optimized for
geostationary satellites of the **GOES-R** family.

### 1.2 Design philosophy

It is designed exclusively to operate in the domain of **views** and
**visual products**. It does not replace physical analysis platforms or
general-purpose GIS tools. Its goal is to offer a simple, very fast, and
conceptually clear workflow for the visual interpretation of satellite
scenes.

---

## 2. Core concepts

Concepts and terms used in the context of this project.

### Image

A numerical representation of a continuous physical scene, organized as a
collection of bands that record the spatial and spectral distribution of
physical quantities — such as radiance, temperature, or reflectance — via
discrete logical elements. [Lira, 2010]

### View

A representation derived from an image, normalized and quantized for
interpretation by the human visual system.

### Product

A view associated with a concept recognizable by the environmental
sciences community (for example: *true color*, *air mass*, *ash*).

### Instant (timestamp)

The **instant** is the temporal moment associated with a satellite scene,
defined by the sensor's effective observation time and represented by a
discrete set of temporal components (year, Julian day, hour, minute,
second).

---

## 3. Installation

### 3.1 Get the code

```bash
git clone https://github.com/asierra/hpsatviews.git
cd hpsatviews
```

### 3.2 Dependencies

* C11-compatible C compiler (gcc recommended, with OpenMP support)
* Libraries:
  - **libnetcdf-dev** - Reading GOES L1b/L2 NetCDF files
  - **libpng-dev** - PNG image generation
  - **libgdal-dev** - COG (Cloud Optimized GeoTIFF) image generation
  - **libwebp-dev** - Reading the background image (night lights) in `night`/`daynite` modes
  - **libm** - Math functions
  - **OpenMP** - Parallelism

On Debian/Ubuntu:

```bash
sudo apt install build-essential libnetcdf-dev libpng-dev libgdal-dev libwebp-dev
```

### 3.3 Build and install

```bash
# Production build (HPC: -O3 -march=native)
make

# Debug build (binary: bin/hpsv_debug)
make DEBUG=1

# Build with Spanish help text
make HPSV_LANG=es

# System-wide install (binary + man page)
sudo make install
```

### 3.4 Verify

```bash
hpsv --version
hpsv --help
```

### 3.5 Tests

The project includes an end-to-end regression test suite that runs `hpsv`
against real sample data and compares the result to reference outputs
(PNG/GeoTIFF) using a tolerant pixel diff.

```bash
# Download sample data (GOES-16, no credentials required)
reproduction/download_sample.sh

# Run the full suite (builds the project if needed)
tests/run_all_tests.sh
```

This same suite runs automatically on every push/pull request to `main`
via GitHub Actions (see the CI badge at the top of this document).

---

## 4. Basic usage

*High Performance Satellite Views* is used from the command line with a
simple syntax:

```bash
hpsv <command> <anchor_file> [options]
```

The **anchor file** in NetCDF format identifies the scene, its instant,
and its path. The system automatically infers the files for the required
bands.

### Example

```bash
hpsv gray OR_ABI-L1b-RadF-M6C13_G16.nc
```

Generates a grayscale view of channel C13.

---

## 5. Advanced usage

### 5.1 Available commands

* `gray` – Grayscale view of a single channel or a linear combination of channels.
* `pseudocolor` – View with a color map applied to a single channel or a linear combination of channels.
* `rgb` – RGB composite from three linear combinations of multiple channels.

### 5.2 Global options

* `--help`

  Shows general help. The next section gives more detail.

* `--list-clips` – Lists predefined geographic clips from a CSV file with
  columns *key, name, ul_x, ul_y, lr_x, lr_y*. Example:
```csv
  mexico,Mexico,-121.3325136900594,32.9450945620932,-83.9198061602870,9.8346808199271
  caribe,Caribe,-93.0476928458730,28.0613844882756,-56.01289145276628,5.12538896303195
```

### 5.3 Common options

* `-a, --alpha`
  Adds an alpha channel for transparency in no-data regions or outside a specific threshold.

* `-c, --clip <value>`
  Crops the image. The value can be:

  * a predefined key (e.g., `mexico`), or
  * explicit coordinates, in decimal degrees, negative west longitude, quoted or comma-separated:
    `"lon_min lat_max lon_max lat_min"`

  Examples:
  ```bash
  # Use a predefined clip key
  hpsv gray -c mexico -o clip.png file.nc

  # With commas (no quotes or spaces)
  hpsv rgb -m ash -c -107.23,22.72,-93.84,14.94 -o clip.png file.nc

  # With spaces (quotes REQUIRED)
  hpsv rgb -m ash -c "-107.23 22.72 -93.84 14.94" -o clip.png file.nc
  ```

* `--clahe`
  Applies Contrast Limited Adaptive Histogram Equalization (CLAHE) with predefined parameters (`8,8,4.0`).

* `--clahe-params <params>`
  Same CLAHE option but lets you specify parameters in the format:
  `tiles_x,tiles_y,clip_limit`

  Example:

  ```bash
  --clahe-params "16,16,5.0"
  ```

* `-g, --gamma <value>`
  Applies gamma correction (default `1.0`, i.e. not applied).
  In RGB mode, accepts 3 values separated by `;` to apply a different
  gamma to each channel (R;G;B):
  ```
  hpsv rgb -g "1.8;1.5;1.2" file.nc
  ```
  With a single value, the same gamma is applied to all 3 channels.


* `-h, --histo`
  Applies global histogram equalization. If it produces saturated/blown-out contrast zones, use CLAHE instead.


* `-o, --out <file>`
  Output file. If not specified, the name is generated automatically.
  Supports patterns with brace-delimited tokens:

  * `{YYYY}` year
  * `{YY}` year (2 digits)
  * `{MM}` month
  * `{DD}` day
  * `{hh}` hour
  * `{mm}` minute
  * `{ss}` second
  * `{JJJ}` Julian day
  * `{TS}` Instant (timestamp) YYYYJJJhhmm
  * `{CH}` channel or band (C01, C02, etc.)
  * `{SAT}` satellite (e.g.: `G16`, `G19`)
  * `{SECTOR}` scan sector: `fd`, `conus`, `m1`, or `m2`
  * `{PROD}` mode's short name (e.g. `truecolor`, `ash`); overridden by `--name` if used

  Example:

  ```bash
  hpsv gray -o "ir_{SAT}_{SECTOR}_{CH}_{YYYY}{MM}{DD}.png" \\
        OR_ABI-L1b-RadC-M6C13_G19_s20253551801183.nc
  # → ir_G19_conus_C13_20251221.png
  ```

* `-G, --geographics`
  Reprojects the output to geographic (latitude/longitude) equirectangular coordinates.

* `-s, --scale <factor>`
  Integer spatial scale factor. Values greater than 1 enlarge the image;
  values less than 1 shrink it (default `1`, i.e. not applied). A value of
  -2 implies a scale of 0.5. **Must use integers only**.

* `-t, --geotiff`
  Generates the output as a georeferenced **Cloud Optimized GeoTIFF
  (COG)**, with internal tiling, overviews, and full projection metadata.
  Compatible with QGIS, GDAL, ArcGIS, and cloud services such as STAC,
  Titiler, and any HTTP client supporting range requests.

  Examples:
  ```bash
	# Explicit option
	hpsv gray -t file.nc

	# Automatic detection by extension
	hpsv gray -o output.tif file.nc
  ```

* `-v, --verbose`
  Enables verbose mode, showing detailed processing information.

### 5.4 *gray* command options

Generates a grayscale view.

* `-i, --invert`
  Inverts the values (white to black).

* `--minmax "<min>,<max>"`
  Fixes the physical value range mapped to 0–255, regardless of the
  data's actual min/max. Useful for comparing images from different times
  or scenes with different dynamic ranges.

  Example: comparable nighttime IR images by fixing the temperature
  range in Kelvin:
  ```bash
  hpsv gray -i -s -4 file_G19_C13.nc -o ir_0600.png --minmax "193.15,313.15"
  hpsv gray -i -s -4 file_G19_C13_1200.nc -o ir_1200.png --minmax "193.15,313.15"
  ```
  Without this option, each image scales independently to its own min and
  max, preventing direct visual comparisons.

### 5.5 *pseudocolor* command options

Maps a color palette onto a grayscale view.

* `-p, --cpt <file>`     Applies a color palette (.cpt file) (default: predefined rainbow palette).
* `-i, --invert`         Inverts the values (min to max).

  Example:
  ```bash
  hpsv pseudocolor -p palette.cpt file_GOES.nc
  ```

### 5.6 *rgb* command options

Generates an RGB composite from linear combinations of multiple bands.

* `-m, --mode <mode>`       Operating mode. Available options:
							`daynite` (default), `truecolor`, `night`, `ash`, `airmass`, `severestorm`, `so2`, `custom`.

* `--rayleigh`              Applies Rayleigh atmospheric correction (daytime visible modes only).
							Uses pyspectral LUTs by default (more accurate).

* `--ray-analytic`          Uses analytic Rayleigh correction instead of LUTs (lighter, less accurate).

* `-f, --full-res`          Uses the highest-resolution channel as reference (more detail, slower).
							By default, uses the lowest-resolution channel (faster, smaller output views).

* `--stretch`               Applies a piecewise contrast stretch similar to the one used by
							geo2grid/Beaufort. Improves tonal differentiation in scenes with
							compressed dynamic range (especially useful with `truecolor`).

* `--sharpen`               Applies *ratio sharpening* to improve the spatial sharpness of the
							green and blue components. For each pixel, computes the ratio between
							its value and the mean of its 2×2 block in the red channel (C02), and
							multiplies that ratio into the green and blue channels. Equivalent to
							satpy/geo2grid's `SelfSharpenedRGB`. The effect is noticeable when
							working at full resolution (`--full-res`) or with geographic clips
							(`--clip`). On a full disk at reduced resolution the benefit is
							imperceptible.

* `-N, --name <label>`      Descriptive product name. Written to the JSON and GeoTIFF metadata as
						the root-level `product` field (alongside `satellite`, `sector`, `timestamp`).
						Also available as the `{PROD}` token in `-o` patterns.
						If omitted, `{PROD}` uses the mode's short name (e.g. `truecolor`) and
						`product` in the JSON uses the mode's description (e.g.
						`"True Color RGB (natural)"`). Accepts the format `short:Long description`
						to set both values independently: the part before `:` goes to `{PROD}` in
						the filename, and the part after `:` to the `product` field in the
						JSON/GeoTIFF. If there's no `:`, the value is used for both.

Especially useful with `--mode custom` to identify the composition.

  Examples:

  ```bash
  # True color with Rayleigh atmospheric correction and CLAHE
  hpsv rgb -m truecolor --rayleigh --clahe file.nc

  # True color with Rayleigh, stretch, and ratio sharpening (sharper detail)
  hpsv rgb -m truecolor --rayleigh --stretch --sharpen file.nc

  # Volcanic ash detection
  hpsv rgb -m ash -o ash.png file.nc

  # Custom composition with a descriptive name in metadata and filename
  hpsv rgb -m custom --expr "C13-C14; C13; -1.0*C15+300" \
        --name "ash:Volcanic ash" -o "{PROD}_{SAT}_{YYYY}{MM}{DD}.png" file.nc
  # → ash_G16_20250101.png
  ```

The `daynite` mode intelligently blends the `truecolor` and `night` modes
with background city lights, using a precise mask based on solar geometry,
and automatically applies Rayleigh correction and contrast enhancement.

For `custom` mode see **Band algebra**.

### 5.7 JSON sidecar file

`hpsv` can write a JSON file with processing metadata alongside the output image, useful for traceability and for integrations such as `mapdrawer`.

**Naming convention and activation:**
* The JSON sidecar is opt-in: it is only generated when `-j`/`--json` is passed.
* If the image is `output.png`, the JSON will be `output.json`.

**JSON content** (real example, `hpsv gray file_CMIP_C13.nc -j -G --clahe -g 1.3`):

```json
{
  "tool": "hpsatviews",
  "version": "1.0.0",
  "satellite": "G16",
  "sector": "conus",
  "timestamp": "2024-08-07T13:02:36Z",
  "product": "CMIP",
  "command": "gray",
  "crs": "EPSG:4326",
  "bounds": [-151.654, 14.571, -52.947, 56.640],
  "geometry": {
    "projection": "EPSG:4326",
    "bbox": [-151.654, 14.571, -52.947, 56.640]
  },
  "channels": [
    {
      "name": "C13",
      "quantity": "brightness_temperature",
      "min": 191.633,
      "max": 301.757,
      "unit": "K"
    }
  ],
  "enhancements": {
    "gamma": 1.3,
    "clahe": true,
    "geographics": true,
    "output_file": "output.png",
    "output_width": 5476,
    "output_height": 2334
  }
}
```

* `crs` reflects the output's actual projection: `EPSG:4326` if reprojected with `-G`/`--both`, `goes16`/`goes17`/`goes18`/`goes19` (or `geostationary`) on the satellite's native grid, or the default value `geographics` when no geometry was computed (a plain PNG without `--clip`, neither GeoTIFF nor reprojection). `bounds`/`geometry.bbox` only appear when geometry was actually computed, and are redundant with each other (the same bounding box in two forms).
* `product` only appears for L2 products (CMIP, ACHA, ACHT, ACTP, CTP, LST, SST); L1b (radiance) files don't include it because they have no "product" identity distinct from the channel.
* `enhancements` adds one key per processing option that was actually applied (among others: `gamma`, `clahe`, `histogram`, `invert`, `rayleigh`/`stretch` in `rgb` mode, `scale`, `palette`, `expression`, `geographics`), plus `output_file`/`output_width`/`output_height`. Unused options simply don't appear.
* **GeoTIFF (`-t`):** only a subset of these metadata fields is embedded as GDAL tags inside the file: `tool`, `satellite`, `sector`, `band`, `scan_time`, `product` (when applicable), and `colormap_min`/`colormap_max`/`colormap_size`/`colormap_units` in pseudocolor. `crs` and `bounds` aren't duplicated as text because the GeoTIFF already represents them natively (geotransform + WKT projection); `command`, `channels` (with min/max/quantity), and `enhancements` only exist in the JSON sidecar.

**Use cases:**
* **Reproducibility:** exact documentation of the applied enhancement parameters (gamma, CLAHE, Rayleigh, etc.) and the source product/channel.
* **Integration:** automation of visualization pipelines (e.g. `mapdrawer`), which consumes `crs`/`bounds`/`product` to locate and classify each image.
* **Traceability:** identifying the satellite, sector, channel(s), product, and projection that generated each image.

### 5.8 Output conventions

If `-o`/`--out` is not specified, a deterministic name is generated based on the "anchor" file's metadata, the bands, and the applied operations:

**Format:** `hpsv_<SAT>[_<SECTOR>]_<YYYYJJJ>_<hhmm>_<COMMAND>_<CH>[_<OPS>].<ext>`

Example:
  ```bash
  hpsv gray OR_ABI-L1b-RadC-M6C13_G16_s20242190300217.nc
  # → hpsv_G16_conus_2024219_0300_gray_C13.png
  ```

### 5.9 Band algebra and custom compositions

`hpsv` lets you define linear combinations of bands on the fly to generate RGB composites or complex single-channel images without generating intermediate files.

**Supported syntax:**
* **Terms with per-band coefficients:** (e.g. `2.0*C13`).
* **Operators:** `+`, `-` between terms.
* **Ranges:** optionally, min and max separated by commas. Computed automatically by default.
* **Separators:** use a semicolon `;` to separate the R, G, and B components (only with the `rgb` command).

#### Usage examples

**1. Single-channel algebra** in the gray or pseudocolor commands.

```bash
hpsv gray anchor_file.nc \
  --expr "C13-C15" \
  --minmax "0.0,100.0"
```

**2. Custom RGB composition.** Define independent formulas for the Red, Green, and Blue channels using `custom` mode. Note the use of quotes to protect spaces and semicolons.

```bash
hpsv rgb anchor_file.nc \
  --mode custom \
  --expr "C13-C14; C13-C11; C13" \
  --minmax "-2,2; -4,2; 240,300" \
  --out "volcanic_ash.png"
```

---

## 6. Technical details

### 6.1 Geometry and geolocation

View generation relies on rigorous geometric formulations. The system implements direct reprojection from geostationary projection to a uniform lat/lon grid (WGS84), with gap handling and automatic inference of domains outside the visible disk. Geographic clipping is optimized when possible, performed before reprojection.

### 6.2 Atmospheric correction (Rayleigh)

HPSATVIEWS incorporates Rayleigh scattering correction for visible
channels, improving the visual fidelity of daytime scenes by removing the
atmosphere's molecular scattering contribution.

**LUT implementation (default, `--rayleigh`).** Uses pre-computed
look-up tables derived from pyspectral (Scheirer et al., 2018), indexed by
three variables: secant of the solar zenith angle, secant of the
satellite zenith angle, and the azimuthal angle difference. The LUTs are
embedded into the binary at compile time to avoid external dependencies.
The azimuth convention follows pyspectral: the LUT is indexed with
`180° − Δφ`, where Δφ is the sun–satellite azimuth difference.

**Analytic implementation (`--ray-analytic`).** A lighter alternative
that computes the correction in real time using the Bucholtz (1995) model
and the Hansen & Travis (1974) Rayleigh phase function. Useful when
maximum accuracy isn't required or when reducing binary size is a
priority.

**Cloud-zone relaxation.** Both implementations incorporate relaxation of
the correction where the red channel's reflectance (C02, 0.64 µm) exceeds
0.20, following pyspectral's criterion. The correction fades out
linearly as reflectance reaches 1.0, avoiding over-correction over clouds
and highly reflective surfaces.

### 6.3 CLAHE

The system includes Contrast Limited Adaptive Histogram Equalization (CLAHE) to improve visual interpretability in scenes with pronounced spatial contrast variations.

### 6.4 True Color composition

The `truecolor` mode generates a natural-color image from three ABI
channels: C01 (0.47 µm, blue), C02 (0.64 µm, red), and C03 (0.865 µm,
near-infrared). Since ABI has no native green channel, one is synthesized
via the linear combination:

$$G = 0.465 \cdot B + 0.465 \cdot R + 0.07 \cdot NIR$$

These coefficients reproduce those used by geo2grid/satpy (Bah et al.,
2018) and provide a perceptually balanced green.

**Piecewise stretch (`--stretch`).** The corrected reflectance is mapped
to digital levels via a piecewise stretch that selectively expands dark
tones and compresses bright ones, improving tonal differentiation in
scenes with compressed dynamic range. The curve is equivalent to the one
used by geo2grid.

### 6.5 Performance

Implemented in C11 (ISO/IEC 9899:2011) with OpenMP parallelization, HPSATVIEWS prioritizes high performance, efficient memory use, and scalability on multi-core systems.

---

## 7. Project status

HPSATVIEWS is under active development, functionally stable, with progressive expansion of capabilities and documentation.

**Future work:** exploring fine-grained GPU parallelism (CUDA/OpenCL) is
under consideration for the most computationally expensive stages
(Rayleigh correction, reprojection), as a complement to the current
OpenMP-based parallelism.

Want to contribute, report a problem, or get support? See
[CONTRIBUTING.md](CONTRIBUTING.md). This project follows the
[Code of Conduct](CODE_OF_CONDUCT.md) based on the Contributor Covenant.

---

## 8. References
- Bah, K., Schmit, T. J., Gerth, J., Cronce, M., otkin, J., & Li, J. (2018).
  GOES-16 Advanced Baseline Imager (ABI) True Color Imagery for Legacy and 
  Non-Traditional Applications. NOAA/CIMSS.
- Bodhaine, B. A., et al. (1999). "On Rayleigh optical depth 
  calculations." *Journal of Atmospheric and Oceanic Technology*, 16(11), 
  1854-1861.
- Bucholtz, A. (1995). Rayleigh-scattering calculations for the terrestrial 
  atmosphere. Applied Optics, 34(15), 2765-2773.
- Hansen, J. E., & Travis, L. D. (1974). Light scattering in planetary 
  atmospheres. Space Science Reviews, 16(4), 527-610.
- Lira Chávez, J. (2010). Tratamiento digital de imágenes 
  multiespectrales (2a ed.). México, D. F.: Instituto de Geofísica, 
  Universidad Nacional Autónoma de México
- Miller, S. D., et al. (2012). "A sight for sore eyes: The return of 
  true color to geostationary satellites." *Bulletin of the American 
  Meteorological Society*, 93(10), 1803-1816.
- Pizer, S. M., et al. (1987). "Adaptive histogram equalization and its 
  variations." *Computer Vision, Graphics, and Image Processing*, 39(3), 
  355-368.
- PySpectral Atmospheric correction Look Up Tables. Available online: 
  https://doi.org/10.5281/zenodo.1205534 (accessed on 2 October 2025) 
- Scheirer, Ronald & Dybbroe, Adam & Raspaud, Martin. (2018). A General 
  Approach to Enhance Short Wave Satellite Imagery by Removing Background 
  Atmospheric Effects. Remote Sensing. 10. 10.3390/rs10040560.   
- Zuiderveld, K. (1994). Contrast Limited Adaptive Histogram 
  Equalization. In P. S. Heckbert (Ed.), Graphics Gems IV (pp. 474–485). 
  Academic Press.
  
---

## 9. How to cite

If HPSATVIEWS is useful in your research or software, please cite it.
Citation metadata (authors, ORCID, version, license) is maintained in
[CITATION.cff](CITATION.cff) — GitHub renders a "Cite this repository"
button from it on the repo's main page.

```bibtex
@software{aguilar_sierra_hpsatviews,
  author    = {Aguilar Sierra, Alejandro},
  title     = {hpsatviews: High Performance Satellite Views},
  version   = {1.0.0},
  year      = {2026},
  publisher = {Zenodo},
  doi       = {10.5281/zenodo.20817974},
  url       = {https://doi.org/10.5281/zenodo.20817974}
}
```

---

## 10. Author and license

```
Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
Laboratorio Nacional de Observación de la Tierra, UNAM

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

See the [LICENSE](LICENSE) file for details.

---

*HPSATVIEWS - High-performance visualization of satellite data*
