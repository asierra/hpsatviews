/* Machine-readable per-run timing record for operational measurement.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Companion to LOG_TIMING: the same measurements the [PERF] log already
 * emits as text are accumulated per stage and appended as one CSV row per
 * run, so a production campaign can be analysed without parsing logs.
 * Disabled unless --timing-csv is given: timing_add() then costs one
 * predicted branch.
 *
 * The stage taxonomy below is deliberately identical for the OpenMP and the
 * CUDA builds. Comparing the two builds column by column is the whole point
 * of the record, so a stage that exists in one build and not the other is a
 * defect, not a detail.
 */
#ifndef HPSATVIEWS_TIMING_H_
#define HPSATVIEWS_TIMING_H_

#include <stdbool.h>

#include "config.h"
#include "datanc.h"
#include "logger.h"

/* Pipeline stages. NAV and GEOM are kept apart from the rest because they
 * are the double-precision work: separating them is what makes the record
 * able to explain why the same code has a different bottleneck per device. */
typedef enum {
    TM_INIT = 0,      /* process startup: CUDA context creation (0 on OpenMP)  */
    TM_OPEN,          /* netCDF open, product identification, attribute reads  */
    TM_READ,          /* file I/O, HDF5 chunk index retrieval, raw chunk fetch */
    TM_DECODE,        /* chunk decompression (libdeflate / netCDF fallback)    */
    TM_UNPACK,        /* scale/offset, fill values, Planck and kappa0 calibration */
    TM_NAV,           /* fixed-grid to lat/lon geolocation          (FP64)     */
    TM_GEOM,          /* solar and satellite viewing geometry       (FP64)     */
    TM_CORRECT,       /* Rayleigh (LUT or analytic), solar zenith normalization*/
    TM_COMPOSE,       /* band algebra, RGB/gray/nocturnal composition, blending*/
    TM_ENHANCE,       /* gamma, stretch, sharpening, CLAHE, resampling         */
    TM_REPROJECT,     /* fixed grid to geographic equirectangular              */
    TM_WRITE,         /* PNG/GeoTIFF encoding and output                       */
    TM_XFER,          /* host-device transfers; always 0 in the OpenMP build   */
    TM_MEM,           /* allocation, first touch and release of the large grids */
    TM_OTHER,         /* timed work that fits no stage above                   */
    TM_STAGE_COUNT
} TimingStage;

/* Per-run identity accompanying the stage times. Filled by the caller just
 * before timing_emit(); string fields may be NULL and numeric fields 0 when
 * unknown, which the CSV renders as an empty field.
 *
 * Fields constant for the binary (host, build, version, commit, library
 * versions, GPU model) are resolved inside timing.c and are not listed here.
 * in_mtime_utc carries the input file's modification time, the arrival
 * instant that turns a processing time into end-to-end latency. */
typedef struct {
    const char *scene_id;     /* timestamp signature, e.g. "s20253231800"      */
    const char *sat;          /* G16, G18, G19                                 */
    const char *scene;        /* F, C, M1, M2                                  */
    const char *subcmd;       /* gray, pseudocolor, rgb                        */
    const char *product;      /* RGB mode or L1b/L2 product name               */
    const char *in_path;      /* anchor file                                   */
    const char *geo;          /* fixed, geographic, both                       */
    long long   in_bytes;     /* total bytes read across channels              */
    double      in_mtime_utc; /* anchor file mtime, epoch seconds              */
    int         n_channels;
    int         nx, ny;       /* output dimensions                             */
    bool        full_res;
    bool        used_cuda;    /* whether this run actually took the GPU path  */
    int         exit_code;
} TimingRow;

/* Fills the identity fields shared by every subcommand from the anchor file
 * and the run's configuration: scene signature, satellite, sector, product,
 * and the anchor's size and modification time. The caller sets what only it
 * knows (output dimensions, channel count, exit code) before emitting. */
void timing_row_from_nc(TimingRow *row, const DataNC *nc, const ProcessConfig *cfg);

/* Enable the record. csv_path NULL or empty leaves timing disabled. */
void timing_enable(const char *csv_path);
bool timing_enabled(void);

/* Accumulate one measurement. Safe to call from several threads; a stage may
 * be fed any number of times (per channel, per band) and the row reports the
 * sum together with the call count. No-op while disabled. */
void timing_add(TimingStage stage, double seconds);

/* Total input bytes read, accumulated as each channel file is opened. The
 * anchor's size alone understates it several-fold: a full-disk composite
 * pulls its siblings too, and C02 at 0.5 km dwarfs the rest. */
void timing_add_bytes(long long bytes);

double      timing_stage_seconds(TimingStage stage);
int         timing_stage_calls(TimingStage stage);
const char *timing_stage_name(TimingStage stage);

/* Append the row. Writes the header first if the file is empty, takes an
 * exclusive lock and emits the record in a single write, so concurrent hpsv
 * processes sharing one file cannot interleave a partial line.
 * Returns 0 on success. No-op returning 0 while disabled. */
int timing_emit(const TimingRow *row);

void timing_cleanup(void);

/* Drop-in replacement for LOG_TIMING that also feeds the record. The text
 * output is byte-identical to LOG_TIMING's, so -v output does not change. */
#define LOG_TIMING_STAGE(stage, elapsed_s, fmt, ...) do { \
    double _tm_elapsed = (double)(elapsed_s); \
    timing_add((stage), _tm_elapsed); \
    LOG_DEBUG("[PERF] " fmt ": %.3f s", ##__VA_ARGS__, _tm_elapsed); \
} while(0)

#endif /* HPSATVIEWS_TIMING_H_ */
