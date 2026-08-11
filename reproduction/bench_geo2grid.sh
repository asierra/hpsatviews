#!/bin/bash
# Cross-tool benchmark: hpsatviews vs geo2grid (SSEC/CIMSS), same host, same
# scene, same product. Companion to bench_server.sh, which only compares hpsv's
# own CPU and CUDA builds.
#
# Usage:
#   reproduction/bench_geo2grid.sh <anchor.nc>
#
# Env overrides:
#   GEO2GRID_HOME  geo2grid bundle root (must contain bin/geo2grid.sh).
#               Already exported on the LANOT servers; the fallback default is
#               /data/cspp/geo2grid_v_1_2 (the dev box).
#   WORKERS     geo2grid --num-workers values to sweep. Default: derived from
#               nproc (4 8 16 32 ... nproc). See "Fairness" below.
#   REPS        Timed runs per configuration. Default 2.
#   OMP_NUM_THREADS  Caps hpsv's threads. Left unset, hpsv uses every core;
#               set it on a shared host so both tools get the same budget.
#   WORKDIR     Scratch for outputs. Default: $TMPDIR or /tmp. Needs ~3 GB
#               free for a full disk (geo2grid writes ~1.1 GB per run).
#   KEEP=1      Keep the last output of each tool (for the product comparison
#               below, or for eyeballing). Default: delete as we go.
#
# ---------------------------------------------------------------------------
# Two things this script exists to get right
#
# 1. SAME PRODUCT. geo2grid's ABI true_color is ALWAYS emitted at 0.5 km with
#    ratio sharpening -- '--match-resolution' does NOT reduce it, because 0.5 km
#    already is the composite's resolution. hpsv defaults to the coarsest input
#    channel (1 km) with no sharpening, i.e. a quarter of the pixels. Timing the
#    two defaults against each other measures nothing. So hpsv is run here with
#    '-f --sharpen --stretch', which is what makes the outputs comparable.
#
# 2. FAIRNESS ON MANY-CORE HOSTS. hpsv's OpenMP loops scale close to linearly;
#    geo2grid's dask graph usually plateaus well before the core count, and its
#    own default is only 4 workers. Pinning geo2grid to one arbitrary value on a
#    64-thread box turns the comparison into a straw man. This script sweeps
#    --num-workers and reports geo2grid's BEST time -- quote that one.
# ---------------------------------------------------------------------------
set -u

ANCHOR="${1:?Usage: $0 <anchor.nc>   (any ABI L1b channel of the scene)}"
[ -r "$ANCHOR" ] || { echo "No such file: $ANCHOR" >&2; exit 1; }

GEO2GRID_HOME="${GEO2GRID_HOME:-/data/cspp/geo2grid_v_1_2}"
G2G="$GEO2GRID_HOME/bin/geo2grid.sh"
REPS="${REPS:-2}"
NCPU="$(nproc)"
HPSV="$(cd "$(dirname "$0")/.." && pwd)/bin/hpsv"

[ -x "$G2G" ]  || { echo "geo2grid not found at $G2G (set GEO2GRID_HOME)" >&2; exit 1; }
[ -x "$HPSV" ] || { echo "hpsv not built: $HPSV (run 'make' first)" >&2; exit 1; }

# Default worker sweep: powers of two up to the core count, plus the core count.
if [ -z "${WORKERS:-}" ]; then
    WORKERS=""
    for w in 4 8 16 32 64 128; do
        [ "$w" -lt "$NCPU" ] && WORKERS="$WORKERS $w"
    done
    WORKERS="$WORKERS $NCPU"
fi

WORKDIR="${WORKDIR:-${TMPDIR:-/tmp}}/bench_g2g_$$"
mkdir -p "$WORKDIR" || exit 1
cleanup() { [ "${KEEP:-0}" = "1" ] || rm -rf "$WORKDIR"; }
trap cleanup EXIT

# --- Locate the sibling channels -------------------------------------------
# Sibling filenames share the _s<start> stamp but NOT the _e/_c stamps, so a
# plain C01->C02 substitution yields a name that does not exist. Glob instead.
DIR="$(cd "$(dirname "$ANCHOR")" && pwd)"
BASE="$(basename "$ANCHOR")"
TS="$(printf '%s' "$BASE" | grep -oE '_s[0-9]{11,}' | head -1 | cut -c3-)"
[ -n "$TS" ] || { echo "Cannot parse the _s<timestamp> out of $BASE" >&2; exit 1; }

band() {  # $1 = 01|02|03  -> absolute path of that channel for this scene
    local pat hit
    pat="$(printf '%s' "$BASE" | sed -E "s/(M[0-9])C[0-9]{2}_.*/\1C$1_*_s${TS}_*.nc/")"
    hit="$(ls "$DIR"/$pat 2>/dev/null | head -1)"
    [ -n "$hit" ] || { echo "Missing channel C$1 for scene s$TS in $DIR" >&2; exit 1; }
    printf '%s' "$hit"
}
C01="$(band 01)"; C02="$(band 02)"; C03="$(band 03)"

# --- Timing helpers ---------------------------------------------------------
walltime() {  # runs "$@" silently, echoes elapsed seconds with 2 decimals
    local s e
    s=$(date +%s.%N); "$@" >/dev/null 2>&1; e=$(date +%s.%N)
    echo "$e $s" | awk '{printf "%.2f", $1-$2}'
}
median() { printf '%s\n' "$@" | sort -n | awk \
    '{a[NR]=$1} END{ if (NR%2) printf "%.2f", a[(NR+1)/2];
                     else printf "%.2f", (a[NR/2]+a[NR/2+1])/2 }'; }

# --- Provenance -------------------------------------------------------------
echo "=============================================================="
echo " hpsatviews vs geo2grid"
echo "=============================================================="
echo "host      : $(hostname)"
echo "CPU       : $NCPU threads (OMP_NUM_THREADS=${OMP_NUM_THREADS:-unset -> all})"
echo "GPU       : $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1 || echo 'N/A')"
echo "hpsv      : $("$HPSV" --version 2>/dev/null | head -1 || echo '?') @ $(git -C "$(dirname "$HPSV")/.." rev-parse --short HEAD 2>/dev/null || echo 'no git')"
if ldd "$HPSV" 2>/dev/null | grep -q cudart; then HAS_CUDA=1; else HAS_CUDA=0; fi
echo "            CUDA build: $([ $HAS_CUDA = 1 ] && echo yes || echo 'no (GPU row will be skipped)')"
echo "geo2grid  : $GEO2GRID_HOME"
echo "scene     : $(basename "$C01" | sed -E 's/_e[0-9]+_c[0-9]+\.nc//')"
echo "product   : true color + Rayleigh + ratio sharpening, 0.5 km, GeoTIFF"
echo "reps      : $REPS   sweep: --num-workers$WORKERS"
echo "workdir   : $WORKDIR"
echo

# --- Warm the page cache for BOTH tools -------------------------------------
# Otherwise whoever runs first pays for the ~600 MB read and the comparison is
# a disk benchmark. Both tools read exactly these three files.
echo "-- warming page cache (C01/C02/C03) --"
for f in "$C01" "$C02" "$C03"; do cat "$f" > /dev/null; done
echo "   done ($(du -ch "$C01" "$C02" "$C03" | tail -1 | cut -f1))"
echo

# --- hpsv -------------------------------------------------------------------
HPSV_ARGS=(rgb "$C01" --mode truecolor --rayleigh -f --sharpen --stretch)

echo "-- hpsv, CPU (OpenMP) --"
t=(); for i in $(seq 1 "$REPS"); do
    v=$(walltime "$HPSV" "${HPSV_ARGS[@]}" -o "$WORKDIR/hpsv_cpu.tif")
    echo "   run $i: $v s"; t+=("$v")
done
HPSV_CPU=$(median "${t[@]}")
[ "${KEEP:-0}" = "1" ] || rm -f "$WORKDIR/hpsv_cpu.tif"

HPSV_GPU=""
if [ "$HAS_CUDA" = 1 ]; then
    echo "-- hpsv, CUDA --"
    t=(); for i in $(seq 1 "$REPS"); do
        v=$(walltime "$HPSV" "${HPSV_ARGS[@]}" --cuda -o "$WORKDIR/hpsv_gpu.tif")
        echo "   run $i: $v s"; t+=("$v")
    done
    HPSV_GPU=$(median "${t[@]}")
    [ "${KEEP:-0}" = "1" ] || rm -f "$WORKDIR/hpsv_gpu.tif"
fi
echo

# --- geo2grid, swept over --num-workers -------------------------------------
G2G_BEST=""; G2G_BEST_W=""
for w in $WORKERS; do
    echo "-- geo2grid, --num-workers $w --"
    t=(); for i in $(seq 1 "$REPS"); do
        rm -f "$WORKDIR"/*_true_color_*.tif
        v=$(cd "$WORKDIR" && walltime "$G2G" -r abi_l1b -w geotiff -p true_color \
              --num-workers "$w" -f "$C01" "$C02" "$C03")
        echo "   run $i: $v s"; t+=("$v")
    done
    m=$(median "${t[@]}")
    echo "   median: $m s"
    if [ -z "$G2G_BEST" ] || awk "BEGIN{exit !($m < $G2G_BEST)}"; then
        G2G_BEST="$m"; G2G_BEST_W="$w"
        if [ "${KEEP:-0}" = "1" ]; then
            mv -f "$WORKDIR"/*_true_color_*.tif "$WORKDIR/g2g_best.tif" 2>/dev/null
        fi
    fi
    rm -f "$WORKDIR"/*_true_color_*.tif
done
echo

# --- Verdict ----------------------------------------------------------------
echo "=============================================================="
printf " geo2grid  (best, --num-workers %-4s) : %8s s\n" "$G2G_BEST_W" "$G2G_BEST"
printf " hpsv CPU                            : %8s s   (%sx)\n" \
       "$HPSV_CPU" "$(awk "BEGIN{printf \"%.2f\", $G2G_BEST/$HPSV_CPU}")"
[ -n "$HPSV_GPU" ] && printf " hpsv CUDA                           : %8s s   (%sx)\n" \
       "$HPSV_GPU" "$(awk "BEGIN{printf \"%.2f\", $G2G_BEST/$HPSV_GPU}")"
echo "=============================================================="
echo
echo "Quote geo2grid's BEST time, not its default (4 workers) -- see 'Fairness'"
echo "in the header. Re-run on each host: these ratios do not transfer."
if [ "${KEEP:-0}" = "1" ]; then
    echo
    echo "Outputs kept in $WORKDIR. To check the two tools agree on the product:"
    echo "  reproduction/compare_g2g_product.sh $WORKDIR/g2g_best.tif $WORKDIR/hpsv_cpu.tif"
fi
