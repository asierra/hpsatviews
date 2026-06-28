---
title: 'hpsatviews: High-performance command-line tool for geostationary satellite imagery'
tags:
  - C11
  - remote sensing
  - satellite imagery
  - GOES-R
  - OpenMP
authors:
  - name: Alejandro Aguilar Sierra
    orcid: 0000-0003-1018-8521
    affiliation: 1
affiliations:
 - name: Laboratorio Nacional de Observación de la Tierra (LANOT), Universidad Nacional Autónoma de México (UNAM)
   index: 1
date: 30 June 2026
bibliography: paper.bib
---

# Summary

`hpsatviews` (`hpsv`) is a command-line tool that turns raw geostationary
weather-satellite data into ready-to-view images. Pointed at a single NetCDF
file from a GOES-R series satellite's Advanced Baseline Imager (ABI), it
automatically locates the sibling spectral channels it needs, applies
atmospheric and contrast corrections, and writes a grayscale, pseudocolor, or
RGB composite — a natural "true color" view, a volcanic-ash or air-mass
product, or a day/night blend with city lights — as a PNG or a georeferenced
Cloud-Optimized GeoTIFF. Following the terminology of @LiraChavez2010, it
treats the source data as an *image*, derives from it a normalized, humanly
interpretable *view*, and labels that view as a *product* (true color, ash,
air mass, ...) once it matches a concept recognized by the environmental
sciences community. Written in C11 and parallelized with OpenMP, `hpsv`
targets near-real-time throughput on commodity hardware: it generates visual
products for human interpretation, and is scoped deliberately narrowly to do
that one task quickly.

# Statement of need

Operational hazard monitoring needs visual satellite products within
seconds of data arrival, repeated continuously as new scenes arrive every
five to fifteen minutes: a volcanic-ash advisory or a hurricane day/night
composite that takes minutes to render has already lost part of its useful
window by the time it reaches a forecaster. `hpsv`'s output products are
distributed operationally, via FTP and web channels, to Mexican agencies
responsible for hazard early warning — including CENAPRED (Centro Nacional
de Prevención de Desastres) and SEMAR (Secretaría de Marina) — for which a
short, predictable turnaround from satellite overpass to delivered image is
a hard operational requirement, not a convenience. The established tools
for generating these products, `satpy` (Pytroll) and `geo2grid`/`Polar2Grid`
(SSEC/CIMSS), are Python libraries built for the breadth of operations a
satellite-data researcher might need: dozens of instrument readers,
resampling backends, and an interactive analysis workflow, at the cost of a
heavier runtime stack. `hpsatviews` targets the narrower, latency-sensitive
niche next to them: a single compiled binary with no Python runtime or
external lookup-table files to load, OpenMP-parallel pixel loops, and
Rayleigh-correction tables embedded at compile time, producing the same
well-established GOES-R products through one CLI invocation per scene. It
is aimed at operational satellite-monitoring groups and image-pipeline
integrators who need a fast, scriptable, dependency-light component for a
larger distribution system, rather than at researchers exploring new
algorithms, who are already well served by `satpy`/`geo2grid`'s
flexibility.

# State of the field

By its own design philosophy, `hpsatviews` does not aim to replace
general-purpose GIS platforms such as QGIS/GDAL or full satellite-data
analysis libraries such as `satpy`; it is scoped exclusively to the domain
of generating standard *views* for human interpretation, as fast as
possible. Within that narrower scope it deliberately reuses the published
methodology of the Python-based geostationary-imaging ecosystem rather than
reinventing it: its true-color green-channel synthesis matches the
coefficients used by `geo2grid`/`satpy` for GOES-R ABI, which lacks a native
green band [@Bah2018; @Miller2016]; its piecewise contrast stretch and
ratio-sharpening ("SelfSharpenedRGB"-equivalent) reproduce `geo2grid`'s; and
its default Rayleigh-correction lookup tables are generated from
`pyspectral` [@Scheirer2018; @PySpectralLUT2018]. What `hpsatviews` changes
is the implementation substrate: a self-contained C11/OpenMP binary instead
of an `xarray`/`dask`-based Python stack, trading `satpy`'s breadth (dozens
of sensors, a general analysis API) for a single family of satellites and a
single use case. Desktop tools such as QGIS or McIDAS-V cover overlapping
visualization ground but are interactive applications, not headless
pipeline components. To the author's knowledge, no other actively
maintained tool occupies this specific niche: a native, OpenMP-parallel CLI
that reproduces the standard GOES-R RGB product catalogue with no runtime
dependency beyond the C libraries it links against.

# Software design

`hpsatviews` is written in strict modern C (C11 with POSIX extensions,
built with `-Wall -Wextra`) and relies on OpenMP parallelization and a small
set of efficient design patterns rather than abstraction layers. The
processing pipeline is linear and explicit (see \autoref{fig:workflow}): parse CLI arguments into an
immutable `ProcessConfig`; load the anchor NetCDF file and infer/load
sibling channels based on the requested command mode (single-channel or RGB composite); sequentially apply optional corrections (Rayleigh atmospheric scattering and CLAHE contrast enhancement); normalize to 8-bit; optionally
reproject from the satellite's fixed grid to a geographic lat/lon grid; write the final image as PNG or GeoTIFF (where essential metadata is automatically embedded within the GeoTIFF tags for seamless extraction by downstream cataloging tools like MapDrawer) and, conditionally, write a JSON metadata sidecar. RGB products
are implemented as a mode table mapping each named product (`truecolor`,
`ash`, `airmass`, `daynite`, `severestorm`, `so2`, ...) to a per-channel
linear combination, plus a `custom` mode that accepts arbitrary band-algebra
expressions (e.g. `"C13-C14"`) so users are not limited to the built-in
catalogue. Two Rayleigh-correction implementations are offered: a default
table lookup derived from `pyspectral` [@Scheirer2018; @PySpectralLUT2018]
and embedded directly in the binary to avoid any runtime data dependency,
and a lighter analytic alternative based on the Bucholtz scattering model
and the Hansen and Travis phase function [@Bucholtz1995; @HansenTravis1974],
with both following the Rayleigh optical-depth formulation of
@Bodhaine1999 and applying cloud relaxation above a reflectance threshold.
Contrast enhancement includes Contrast Limited Adaptive Histogram
Equalization following @Pizer1987 and @Zuiderveld1994. The central
trade-off throughout is startup latency and dependency footprint versus
runtime flexibility: embedding the Rayleigh LUTs at compile time means a
rebuild is required to change atmospheric models, but it removes any need
for Python, `pyspectral`, or external data files at run time; likewise,
OpenMP loop parallelism trades multi-node or GPU scalability for simplicity
on the single multi-core machines typical of an image-ingestion pipeline.

![Core processing workflow of `hpsatviews`. The execution path diverges based on the requested command mode, followed by sequential, optional stages for Rayleigh atmospheric correction, CLAHE contrast enhancement, and geographic reprojection before writing the final image output and an optional JSON metadata sidecar.\label{fig:workflow}](processing_workflow.png){ width="55%" }


# Research impact statement

`hpsatviews`' output products are already in operational use (see \autoref{fig:radiometric} for examples of application-specific composites): generated
composites are distributed via FTP and web channels to Mexican government
agencies responsible for hazard monitoring and early warning, including
CENAPRED (disaster prevention) and SEMAR (the navy), for whom the latency
reduction described above is operationally load-bearing rather than
incidental. It reached its first public release (v1.0.0) in June 2026,
archived on Zenodo under a persistent DOI, and is also a component of a
larger imagery pipeline at LANOT/UNAM: its optional JSON/GeoTIFF metadata
sidecar is designed for, and consumed by, a companion automation tool
[`mapdrawer`](https://github.com/asierra/LANOT_tools), that catalogues and serves generated images using the
embedded coordinate-reference-system, bounding-box, and product fields. 


![Composite panel of `hpsatviews` radiometric outputs generated from GOES ABI data: (a) Night composite with meteorological colormap; (b) Volcanic-ash RGB composite isolating SO2 and ash plumes; (c) Air mass RGB composite illustrating synoptic-scale dynamics; (d) Pseudocolor enhancement of a Level-2 Cloud Top Pressure product; (e) Grayscale view of the high-resolution visible channel with Contrast Limited Adaptive Histogram Equalization (CLAHE) applied to enhance local cloud textures; (f) Severe Storms RGB composite highlighting deep convection.\label{fig:radiometric}](radiometric.jpg)


As a first public release under its current name, it does not yet have
independent external citations in the literature; its demonstrated
near-term significance is this existing operational role in Mexican
disaster-prevention and maritime early-warning distribution, plus its
general fitness — given its narrow scope and minimal dependency footprint —
as a fast component for similar operational infrastructure elsewhere, as it is capable of generating localized, operationally relevant views (e.g., North Atlantic and the Gulf of Mexico) from a single execution pipeline (see \autoref{fig:reprojection}).


![Geometric processing workflow in `hpsatviews`: (Left) A true color RGB composite generated in the native geostationary projection of the GOES-R Advanced Baseline Imager, showcasing atmospheric Rayleigh correction; (Center) The same full disk computationally reprojected into a uniform geographic latitude/longitude grid (WGS84); (Right) Arbitrary regional crops extracted from the reprojected domain, demonstrating the tool's ability to generate localized, operationally relevant views (e.g., North Atlantic and the Gulf of Mexico) from a single execution pipeline.\label{fig:reprojection}](reprojection.jpg)

Future development for `hpsatviews` will focus on expanding support to additional 
satellite platforms beyond the GOES-R series. Additionally, 
exploring fine-grained GPU parallelism is under consideration 
for the most computationally intensive stages—specifically Rayleigh atmospheric 
correction and geometric reprojection—to complement and extend the current 
OpenMP-based multi-core parallelization substrate.

# AI usage disclosure

The initial design and overall philosophy of this project are entirely human. As the project evolved, however, large-scale refactoring, the diagnosis of elusive bugs, and the implementation of intricate algorithms benefited substantially from AI assistance — primarily Gemini Pro (versions 2.4 to 3.1) and Claude Sonnet (versions 4.5 to 4.6). AI was also used to draft portions of this paper. In all cases, the human author reviewed, edited, and validated every AI-assisted output and retained sole responsibility for all core design decisions.

# Acknowledgements

This work was developed at the Laboratorio Nacional de Observación de la
Tierra (LANOT), Universidad Nacional Autónoma de México (UNAM), with support
from Laboratorio Nacional SECIHTI 2025-2027, grant ApoyoLN-2025-C-102.

# References
