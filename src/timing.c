/* Machine-readable per-run timing record. See include/timing.h.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "timing.h"
#include "metadata.h"
#include "version.h"

#include <errno.h>
#include <fcntl.h>
#include <omp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <gdal.h>
#include <hdf5.h>
#include <netcdf.h>

#ifdef HPSV_CUDA
#include <cuda_runtime_api.h>
#endif

/* Schema version: bump when columns are added or reordered, so a campaign
 * spanning a change can still be split cleanly at analysis time. */
#define TIMING_SCHEMA 1

#define TIMING_PATH_MAX 1024
#define TIMING_LINE_MAX 8192

static char   g_csv_path[TIMING_PATH_MAX];
static bool   g_enabled = false;
static double g_t0 = 0.0;          /* monotonic, for the run's wall time */
static double g_start_epoch = 0.0; /* wall clock at enable, for the ISO stamp */

static double g_stage[TM_STAGE_COUNT];
static int    g_calls[TM_STAGE_COUNT];
static long long g_bytes = 0;

static const char *const kStageName[TM_STAGE_COUNT] = {
    "init", "open", "read", "decode", "unpack", "nav", "geom", "correct",
    "compose", "enhance", "reproject", "write", "xfer", "mem", "other"
};

const char *timing_stage_name(TimingStage stage) {
    if (stage < 0 || stage >= TM_STAGE_COUNT) return "invalid";
    return kStageName[stage];
}

static double now_epoch(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

void timing_enable(const char *csv_path) {
    if (csv_path == NULL || csv_path[0] == '\0') return;

    if (strlen(csv_path) >= TIMING_PATH_MAX) {
        LOG_WARN("Timing CSV path too long, record disabled: %s", csv_path);
        return;
    }
    snprintf(g_csv_path, sizeof(g_csv_path), "%s", csv_path);

    memset(g_stage, 0, sizeof(g_stage));
    memset(g_calls, 0, sizeof(g_calls));
    g_bytes = 0;
    g_t0 = omp_get_wtime();
    g_start_epoch = now_epoch();
    g_enabled = true;
    LOG_INFO("Timing record enabled: %s", g_csv_path);
}

bool timing_enabled(void) { return g_enabled; }

void timing_add(TimingStage stage, double seconds) {
    if (!g_enabled) return;
    if (stage < 0 || stage >= TM_STAGE_COUNT) return;
#pragma omp atomic
    g_stage[stage] += seconds;
#pragma omp atomic
    g_calls[stage] += 1;
}

void timing_add_bytes(long long bytes) {
    if (!g_enabled || bytes <= 0) return;
#pragma omp atomic
    g_bytes += bytes;
}

double timing_stage_seconds(TimingStage stage) {
    if (!g_enabled || stage < 0 || stage >= TM_STAGE_COUNT) return 0.0;
    return g_stage[stage];
}

int timing_stage_calls(TimingStage stage) {
    if (!g_enabled || stage < 0 || stage >= TM_STAGE_COUNT) return 0;
    return g_calls[stage];
}

/* ---------------------------------------------------------------- helpers */

static void iso_utc(double epoch, char *buf, size_t n) {
    time_t secs = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&secs, &tm_utc);
    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    int ms = (int)((epoch - (double)secs) * 1000.0);
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    snprintf(buf, n, "%s.%03dZ", base, ms);
}

/* Appends a CSV field, quoting only when the value needs it. Fields come from
 * filenames and metadata, so a comma is unlikely but not impossible. */
static void csv_field(char *out, size_t n, size_t *len, const char *value) {
    const char *v = (value != NULL) ? value : "";
    bool needs_quote = (strpbrk(v, ",\"\n") != NULL);
    size_t pos = *len;

    if (pos > 0 && pos + 1 < n) out[pos++] = ',';

    if (needs_quote && pos + 1 < n) out[pos++] = '"';
    for (const char *p = v; *p != '\0' && pos + 2 < n; p++) {
        if (*p == '"' && needs_quote && pos + 2 < n) out[pos++] = '"';
        out[pos++] = *p;
    }
    if (needs_quote && pos + 1 < n) out[pos++] = '"';
    out[pos] = '\0';
    *len = pos;
}

static void csv_num(char *out, size_t n, size_t *len, const char *fmt, ...) {
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    csv_field(out, n, len, tmp);
}

static const char *build_name(void) {
#ifdef HPSV_CUDA
    return "cuda";
#else
    return "openmp";
#endif
}

/* GPU model, empty in the OpenMP build or when no device answers. Queried
 * lazily and only once: in a run that used the GPU a context already exists,
 * and in one that did not, a failure here must stay harmless. */
static const char *gpu_name(void) {
#ifdef HPSV_CUDA
    static char name[sizeof(((struct cudaDeviceProp *)0)->name)];
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        name[0] = '\0';
        int count = 0;
        if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
            int dev = 0;
            struct cudaDeviceProp prop;
            if (cudaGetDevice(&dev) == cudaSuccess &&
                cudaGetDeviceProperties(&prop, dev) == cudaSuccess) {
                snprintf(name, sizeof(name), "%s", prop.name);
            }
        }
    }
    return name;
#else
    return "";
#endif
}

static const char *hdf5_version(void) {
    static char buf[32];
    unsigned maj = 0, min = 0, rel = 0;
    if (H5get_libversion(&maj, &min, &rel) < 0) return "";
    snprintf(buf, sizeof(buf), "%u.%u.%u", maj, min, rel);
    return buf;
}

static const char *git_commit(void) {
#ifdef HPSV_GIT_COMMIT
    return HPSV_GIT_COMMIT;
#else
    return "";
#endif
}

static void write_header(int fd) {
    char hdr[TIMING_LINE_MAX];
    size_t len = 0;
    len += (size_t)snprintf(hdr, sizeof(hdr),
                            "#hpsv-timing schema=%d\n", TIMING_SCHEMA);

    const char *cols =
        "t_start_utc,t_end_utc,host,build,path,hpsv_version,git_commit,gpu,"
        "hdf5_ver,netcdf_ver,gdal_ver,"
        "scene_id,sat,scene,subcmd,product,nx,ny,full_res,geo,"
        "in_path,in_bytes,n_channels,in_mtime_utc,latency_s,t_total";
    len += (size_t)snprintf(hdr + len, sizeof(hdr) - len, "%s", cols);

    for (int s = 0; s < TM_STAGE_COUNT; s++)
        len += (size_t)snprintf(hdr + len, sizeof(hdr) - len, ",t_%s", kStageName[s]);
    for (int s = 0; s < TM_STAGE_COUNT; s++)
        len += (size_t)snprintf(hdr + len, sizeof(hdr) - len, ",n_%s", kStageName[s]);

    snprintf(hdr + len, sizeof(hdr) - len,
             ",load1,omp_threads,mem_peak_mb,exit_code\n");

    if (write(fd, hdr, strlen(hdr)) < 0)
        LOG_WARN("Could not write timing CSV header: %s", strerror(errno));
}

/* Scene signature "sYYYYDDDHHMM", the join key between the records of
 * different hosts and builds.
 *
 * Deliberately not find_id_from_name(): that one keeps 11 characters, one
 * short of the full minute, which is enough to find sibling channels of a
 * full disk but collapses ten consecutive mesoscale scenes onto the same
 * key. A join key has to identify the scene exactly. */
static const char *scene_signature(const char *path) {
    static char id[16];
    id[0] = '\0';
    if (path == NULL) return id;

    const char *s_pos = strstr(path, "_s");
    if (s_pos == NULL || strlen(s_pos) < 13) return id;

    snprintf(id, sizeof(id), "%.12s", s_pos + 1);
    return id;
}

/* netCDF reports itself as "4.9.3 of Feb 14 2025 ..."; keep the number. */
static const char *netcdf_version(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%s", nc_inq_libvers());
    char *space = strchr(buf, ' ');
    if (space != NULL) *space = '\0';
    return buf;
}

void timing_row_from_nc(TimingRow *row, const DataNC *nc, const ProcessConfig *cfg) {
    if (row == NULL) return;

    if (cfg != NULL) {
        row->in_path   = cfg->input_file;
        row->subcmd    = cfg->command;
        row->product   = cfg->strategy;
        row->full_res  = cfg->use_full_res;
        row->used_cuda = cfg->use_cuda;
        row->geo = cfg->save_both ? "both"
                 : (cfg->do_reprojection ? "geographic" : "fixed");
        row->scene_id = scene_signature(cfg->input_file);

        struct stat st;
        if (cfg->input_file != NULL && stat(cfg->input_file, &st) == 0) {
            row->in_bytes = (long long)st.st_size;
            row->in_mtime_utc = (double)st.st_mtime;
        }
    }

    if (nc != NULL) {
        row->sat = metadata_sat_name(nc->sat_id);
        row->scene = metadata_sector_name(nc->sector_id);
        /* For rgb the product is the composite mode, already in cfg->strategy;
         * for gray/pseudocolor it is the L2 product code read from the file. */
        const bool is_rgb = (cfg != NULL && cfg->command != NULL &&
                             strcmp(cfg->command, "rgb") == 0);
        if (!is_rgb && nc->product_name != NULL && nc->product_name[0] != '\0')
            row->product = nc->product_name;
    }
}

/* ------------------------------------------------------------------ emit  */

int timing_emit(const TimingRow *row) {
    if (!g_enabled || row == NULL) return 0;

    const double t_total = omp_get_wtime() - g_t0;
    const double end_epoch = now_epoch();

    char t_start[40], t_end[40];
    iso_utc(g_start_epoch, t_start, sizeof(t_start));
    iso_utc(end_epoch, t_end, sizeof(t_end));

    char host[128] = "";
    if (gethostname(host, sizeof(host) - 1) != 0) host[0] = '\0';

    double loads[3] = {0.0, 0.0, 0.0};
    if (getloadavg(loads, 1) < 0) loads[0] = 0.0;

    struct rusage ru;
    long mem_peak_mb = 0;
    if (getrusage(RUSAGE_SELF, &ru) == 0) mem_peak_mb = ru.ru_maxrss / 1024;

    char line[TIMING_LINE_MAX];
    size_t len = 0;
    line[0] = '\0';

    csv_field(line, sizeof(line), &len, t_start);
    csv_field(line, sizeof(line), &len, t_end);
    csv_field(line, sizeof(line), &len, host);
    csv_field(line, sizeof(line), &len, build_name());
    csv_field(line, sizeof(line), &len, row->used_cuda ? "gpu" : "cpu");
    csv_field(line, sizeof(line), &len, HPSV_VERSION);
    csv_field(line, sizeof(line), &len, git_commit());
    csv_field(line, sizeof(line), &len, gpu_name());
    csv_field(line, sizeof(line), &len, hdf5_version());
    csv_field(line, sizeof(line), &len, netcdf_version());
    csv_field(line, sizeof(line), &len, GDALVersionInfo("RELEASE_NAME"));

    csv_field(line, sizeof(line), &len, row->scene_id);
    csv_field(line, sizeof(line), &len, row->sat);
    csv_field(line, sizeof(line), &len, row->scene);
    csv_field(line, sizeof(line), &len, row->subcmd);
    csv_field(line, sizeof(line), &len, row->product);
    csv_num(line, sizeof(line), &len, "%d", row->nx);
    csv_num(line, sizeof(line), &len, "%d", row->ny);
    csv_num(line, sizeof(line), &len, "%d", row->full_res ? 1 : 0);
    csv_field(line, sizeof(line), &len, row->geo);

    csv_field(line, sizeof(line), &len, row->in_path);
    csv_num(line, sizeof(line), &len, "%lld",
            (g_bytes > 0) ? g_bytes : row->in_bytes);
    csv_num(line, sizeof(line), &len, "%d", row->n_channels);
    if (row->in_mtime_utc > 0.0) {
        char mtime[40];
        iso_utc(row->in_mtime_utc, mtime, sizeof(mtime));
        csv_field(line, sizeof(line), &len, mtime);
        /* End-to-end latency: from the input file landing on disk to the
         * output being available. The number an operational reader cares
         * about, and the one hpsv alone cannot measure. */
        csv_num(line, sizeof(line), &len, "%.3f", end_epoch - row->in_mtime_utc);
    } else {
        csv_field(line, sizeof(line), &len, "");
        csv_field(line, sizeof(line), &len, "");
    }
    csv_num(line, sizeof(line), &len, "%.3f", t_total);

    for (int s = 0; s < TM_STAGE_COUNT; s++)
        csv_num(line, sizeof(line), &len, "%.3f", g_stage[s]);
    for (int s = 0; s < TM_STAGE_COUNT; s++)
        csv_num(line, sizeof(line), &len, "%d", g_calls[s]);

    csv_num(line, sizeof(line), &len, "%.2f", loads[0]);
    csv_num(line, sizeof(line), &len, "%d", omp_get_max_threads());
    csv_num(line, sizeof(line), &len, "%ld", mem_peak_mb);
    csv_num(line, sizeof(line), &len, "%d", row->exit_code);

    if (len + 2 >= sizeof(line)) {
        LOG_WARN("Timing row truncated, not written");
        return -1;
    }
    line[len++] = '\n';
    line[len] = '\0';

    int fd = open(g_csv_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        LOG_WARN("Could not open timing CSV '%s': %s", g_csv_path, strerror(errno));
        return -1;
    }

    /* Several hpsv processes share one file in production. The lock covers
     * the emptiness test as well as the write, so exactly one of them writes
     * the header; O_APPEND plus a single write() keeps rows whole. */
    if (flock(fd, LOCK_EX) != 0)
        LOG_WARN("Could not lock timing CSV: %s", strerror(errno));

    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size == 0) write_header(fd);

    int rc = 0;
    if (write(fd, line, len) < 0) {
        LOG_WARN("Could not write timing row: %s", strerror(errno));
        rc = -1;
    }

    flock(fd, LOCK_UN);
    close(fd);
    return rc;
}

void timing_cleanup(void) {
    g_enabled = false;
    g_csv_path[0] = '\0';
}
