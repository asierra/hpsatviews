#!/bin/bash
# Cross-tool benchmark: hpsatviews vs geo2grid (SSEC/CIMSS), same host, same
# scene, same product. Companion to bench_server.sh, which only compares hpsv's
# own CPU and CUDA builds.
#
# Usage:
#   reproduction/bench_timebudget.sh <anchor.nc>   # builds both, fills bench_bin/
#   reproduction/bench_geo2grid.sh <anchor.nc>
#
# Env overrides:
#   GEO2GRID_HOME  geo2grid bundle root (must contain bin/geo2grid.sh).
#               Already exported on the LANOT servers; the fallback default is
#               /data/cspp/geo2grid_v_1_2 (the dev box).
#   HPSV_CPU_BIN   hpsv binary for the CPU row. Default bench_bin/hpsv-openmp,
#               the build without CUDA that bench_timebudget.sh leaves there.
#   HPSV_GPU_BIN   hpsv binary for the GPU row. Default bench_bin/hpsv-cuda. If
#               it does not exist the GPU row is skipped.
#   WORKERS     geo2grid --num-workers values to sweep. Default: derived from
#               nproc (4 8 16 32 ... nproc). See "Fairness" below.
#   REPS        Timed runs per configuration. Default 5, so that every row can
#               be quoted with its spread and not only a median -- a median of
#               two runs is just their mean. On the 64-thread A30 host a full
#               sweep at 5 takes about 25 minutes, most of it geo2grid at 4
#               workers.
#   RESULTS     CSV receiving every timed run, one row each. Default
#               bench_g2g_<host>_<date>_<time>.csv in the current directory.
#               hpsv's own --timing-csv record is written beside it as
#               <RESULTS>_hpsv_timing.csv; see "One clock" below.
#   OMP_NUM_THREADS  Caps hpsv's threads. Left unset, hpsv uses every core;
#               set it on a shared host so both tools get the same budget.
#   WORKDIR     Scratch for outputs. Default: $TMPDIR or /tmp. Needs ~3 GB
#               free for a full disk (geo2grid writes ~1.1 GB per run).
#   KEEP=1      Keep the last output of each tool (for the product comparison
#               below, or for eyeballing). Default: delete as we go.
#
# ---------------------------------------------------------------------------
# Four things this script exists to get right
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
#
# 3. ONE CLOCK FOR BOTH TOOLS. Every row, hpsv's included, is timed from outside
#    the process, from exec to exit. hpsv's --timing-csv total is a different
#    clock: it starts once the configuration is parsed and stops when the row is
#    written, so it leaves out loading the executable and its libraries, and the
#    release of memory and of the CUDA context at exit -- 0.1-0.3 s per run on a
#    development host. Quoting it against geo2grid's external time would compare
#    two clocks. The record is still written, because it is what verifies point
#    4, and it lets the difference between the two clocks be measured.
#
# 4. TWO BUILDS, ONE REVISION. The CPU row is timed with the build WITHOUT
#    CUDA. A CUDA binary running the CPU path still loads the CUDA libraries,
#    which cost it about 0.2 s of wall time on a development host for work it
#    does not do. Two binaries bring back the paper's first failure mode -- a
#    build left over from another revision -- so each timed run is checked
#    against its own record: the CPU row must say build=openmp path=cpu, the GPU
#    row build=cuda path=gpu (a silent fallback is otherwise invisible here,
#    since output is discarded), and both must carry the same commit.
#
# Each engine gets one discarded run before its timed ones, on top of the
# page-cache warm-up, so that no timed run pays for first-use costs of its own:
# the driver's state for hpsv on the GPU, Python's import caches for geo2grid.
# ---------------------------------------------------------------------------
set -u

ANCHOR="${1:?Usage: $0 <anchor.nc>   (any ABI L1b channel of the scene)}"
[ -r "$ANCHOR" ] || { echo "No such file: $ANCHOR" >&2; exit 1; }

GEO2GRID_HOME="${GEO2GRID_HOME:-/data/cspp/geo2grid_v_1_2}"
G2G="$GEO2GRID_HOME/bin/geo2grid.sh"
REPS="${REPS:-5}"
NCPU="$(nproc)"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
HPSV_CPU_BIN="${HPSV_CPU_BIN:-$REPO/bench_bin/hpsv-openmp}"
HPSV_GPU_BIN="${HPSV_GPU_BIN:-$REPO/bench_bin/hpsv-cuda}"

[ -x "$G2G" ]  || { echo "geo2grid not found at $G2G (set GEO2GRID_HOME)" >&2; exit 1; }
[ -x "$HPSV_CPU_BIN" ] || {
    echo "No CPU build at $HPSV_CPU_BIN." >&2
    echo "Run reproduction/bench_timebudget.sh first, which leaves both builds in" >&2
    echo "bench_bin/, or point HPSV_CPU_BIN at a build without CUDA." >&2
    exit 1; }
if [ -x "$HPSV_GPU_BIN" ]; then HAS_CUDA=1; else HAS_CUDA=0; fi

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

HOST="$(hostname -s)"
CHECKOUT="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo no-git)"
if [ "$CHECKOUT" != no-git ] && ! git -C "$REPO" diff --quiet HEAD 2>/dev/null; then
    CHECKOUT="$CHECKOUT-dirty"
fi
RESULTS="${RESULTS:-bench_g2g_${HOST}_$(date +%Y%m%d_%H%M).csv}"
TIMING="${RESULTS%.csv}_hpsv_timing.csv"
[ -s "$RESULTS" ] || echo "host,commit,tool,config,rep,wall_s,t_total_s" > "$RESULTS"

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
walltime() {  # runs "$@" silently, echoes elapsed seconds with 3 decimals
    local s e
    s=$(date +%s.%N); "$@" >/dev/null 2>&1; e=$(date +%s.%N)
    echo "$e $s" | awk '{printf "%.3f", $1-$2}'
}
median() { printf '%s\n' "$@" | sort -n | awk \
    '{a[NR]=$1} END{ if (NR%2) printf "%.2f", a[(NR+1)/2];
                     else printf "%.2f", (a[NR/2]+a[NR/2+1])/2 }'; }
spread() { printf '%s\n' "$@" | sort -n | awk \
    'NR==1{lo=$1} {hi=$1} END{printf "%.2f-%.2f", lo, hi}'; }
record() { echo "$HOST,$1,$2,$3,$4,$5,${6:-}" >> "$RESULTS"; }

# The --timing-csv record, read by column name so a schema change cannot shift it.
data_rows() { [ -f "$TIMING" ] || { echo 0; return; }
              grep -v -e '^#' -e '^t_start_utc' "$TIMING" | wc -l; }
last_field() {  # $1 = column name -> its value in the last row
    grep -v '^#' "$TIMING" | awk -F, -v col="$1" \
        'NR==1{for(i=1;i<=NF;i++) if($i==col) c=i; next} {v=$c} END{print v}'
}

# time_hpsv <cpu|gpu> <binary> <extra hpsv args...>
# One discarded run, then REPS timed ones. Sets MED, SPREAD, PATH_OK and
# BIN_COMMIT (the commit the binary was built from, as its record reports it).
time_hpsv() {
    local label="$1" bin="$2"; shift 2
    local out="$WORKDIR/hpsv_$label.tif" want_path=cpu want_build=openmp
    local i v tt before bad=0 commits=""
    [ "$label" = gpu ] && { want_path=gpu; want_build=cuda; }
    "$bin" "${HPSV_ARGS[@]}" "$@" -o "$out" >/dev/null 2>&1
    echo "   warm-up run done (discarded)"
    t=(); local tts=()
    for i in $(seq 1 "$REPS"); do
        before=$(data_rows)
        v=$(walltime "$bin" "${HPSV_ARGS[@]}" "$@" -o "$out" --timing-csv "$TIMING")
        if [ "$(data_rows)" -eq $((before + 1)) ]; then
            tt=$(last_field t_total)
            BIN_COMMIT=$(last_field git_commit)
            commits="$commits $BIN_COMMIT"
            [ "$(last_field path)" = "$want_path" ] \
                && [ "$(last_field build)" = "$want_build" ] \
                && [ "$(last_field exit_code)" = 0 ] || bad=$((bad + 1))
        else
            tt=""; BIN_COMMIT="?"; bad=$((bad + 1))
        fi
        echo "   run $i: $v s   (in-process t_total ${tt:-?} s)"
        t+=("$v"); [ -n "$tt" ] && tts+=("$tt")
        record "$BIN_COMMIT" hpsv "$label" "$i" "$v" "$tt"
    done
    # Every run of one row must come from one build.
    [ "$(printf '%s\n' $commits | sort -u | wc -l)" -le 1 ] || bad=$((bad + 1))
    MED=$(median "${t[@]}"); SPREAD=$(spread "${t[@]}")
    echo "   median: $MED s  [$SPREAD]"
    if [ "${#tts[@]}" -gt 0 ]; then
        echo "   in-process t_total median: $(median "${tts[@]}") s --" \
             "the external clock exceeds it by" \
             "$(awk "BEGIN{printf \"%.2f\", $MED - $(median "${tts[@]}")}") s"
    fi
    if [ "$bad" -eq 0 ]; then
        PATH_OK=1
        echo "   verified: all $REPS runs were build=$want_build path=$want_path," \
             "commit $BIN_COMMIT, exit 0"
    else
        PATH_OK=0
        echo "   !! $bad check(s) failed: expected build=$want_build path=$want_path" >&2
        echo "   !! and exit 0 on every run (see $TIMING). Not a $want_path number." >&2
    fi
    [ "${KEEP:-0}" = "1" ] || rm -f "$out"
}

# --- Provenance -------------------------------------------------------------
echo "=============================================================="
echo " hpsatviews vs geo2grid"
echo "=============================================================="
echo "host      : $(hostname)"
echo "CPU       : $NCPU threads (OMP_NUM_THREADS=${OMP_NUM_THREADS:-unset -> all})"
echo "GPU       : $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1 || echo 'N/A')"
echo "hpsv      : $("$HPSV_CPU_BIN" --version 2>/dev/null | head -1 || echo '?')"
echo "  CPU row : $HPSV_CPU_BIN"
echo "  GPU row : $([ $HAS_CUDA = 1 ] && echo "$HPSV_GPU_BIN" || echo "none at $HPSV_GPU_BIN (GPU row skipped)")"
echo "checkout  : $CHECKOUT"
echo "geo2grid  : $GEO2GRID_HOME"
echo "scene     : $(basename "$C01" | sed -E 's/_e[0-9]+_c[0-9]+\.nc//')"
echo "product   : true color + Rayleigh + ratio sharpening, 0.5 km, GeoTIFF"
echo "reps      : $REPS timed + 1 discarded per engine   sweep: --num-workers$WORKERS"
echo "workdir   : $WORKDIR"
echo "results   : $RESULTS"
echo "            $TIMING"
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

echo "-- hpsv, CPU (build without CUDA) --"
time_hpsv cpu "$HPSV_CPU_BIN"
HPSV_CPU="$MED"; HPSV_CPU_SPREAD="$SPREAD"; HPSV_CPU_OK="$PATH_OK"; CPU_COMMIT="$BIN_COMMIT"

HPSV_GPU=""; GPU_COMMIT=""
if [ "$HAS_CUDA" = 1 ]; then
    echo "-- hpsv, CUDA --"
    time_hpsv gpu "$HPSV_GPU_BIN" --cuda
    HPSV_GPU="$MED"; HPSV_GPU_SPREAD="$SPREAD"; HPSV_GPU_OK="$PATH_OK"; GPU_COMMIT="$BIN_COMMIT"
fi

# One revision for both rows, or neither row is quotable against the other.
SAME_REV=1
if [ "$HAS_CUDA" = 1 ] && [ "$CPU_COMMIT" != "$GPU_COMMIT" ]; then
    SAME_REV=0
    echo "   !! the CPU build is from $CPU_COMMIT and the CUDA build from" >&2
    echo "   !! $GPU_COMMIT: not one revision. Rebuild both with bench_timebudget.sh." >&2
fi
if [ "$CHECKOUT" != no-git ] && [ "$CPU_COMMIT" != "${CHECKOUT%-dirty}" ]; then
    echo "   NOTE: the builds report commit $CPU_COMMIT but the checkout is at $CHECKOUT."
fi
echo

# --- geo2grid, swept over --num-workers -------------------------------------
echo "-- geo2grid, warm-up at --num-workers $NCPU (discarded) --"
(cd "$WORKDIR" && "$G2G" -r abi_l1b -w geotiff -p true_color \
     --num-workers "$NCPU" -f "$C01" "$C02" "$C03" >/dev/null 2>&1)
rm -f "$WORKDIR"/*_true_color_*.tif

G2G_BEST=""; G2G_BEST_W=""; G2G_BEST_SPREAD=""
for w in $WORKERS; do
    echo "-- geo2grid, --num-workers $w --"
    t=(); for i in $(seq 1 "$REPS"); do
        rm -f "$WORKDIR"/*_true_color_*.tif
        v=$(cd "$WORKDIR" && walltime "$G2G" -r abi_l1b -w geotiff -p true_color \
              --num-workers "$w" -f "$C01" "$C02" "$C03")
        echo "   run $i: $v s"; t+=("$v")
        record "$CHECKOUT" geo2grid "workers=$w" "$i" "$v"
    done
    m=$(median "${t[@]}")
    echo "   median: $m s  [$(spread "${t[@]}")]"
    if [ -z "$G2G_BEST" ] || awk "BEGIN{exit !($m < $G2G_BEST)}"; then
        G2G_BEST="$m"; G2G_BEST_W="$w"; G2G_BEST_SPREAD="$(spread "${t[@]}")"
        if [ "${KEEP:-0}" = "1" ]; then
            mv -f "$WORKDIR"/*_true_color_*.tif "$WORKDIR/g2g_best.tif" 2>/dev/null
        fi
    fi
    rm -f "$WORKDIR"/*_true_color_*.tif
done
echo

# --- Verdict ----------------------------------------------------------------
flag() { [ "$1" = 1 ] && [ "$SAME_REV" = 1 ] || echo '  !! UNVERIFIED'; }
echo "=============================================================="
echo " external wall time, median of $REPS [min-max]"
printf " geo2grid  (best, --num-workers %-4s) : %8s s  [%s]\n" \
       "$G2G_BEST_W" "$G2G_BEST" "$G2G_BEST_SPREAD"
printf " hpsv CPU                            : %8s s  [%s]  (%sx)%s\n" \
       "$HPSV_CPU" "$HPSV_CPU_SPREAD" "$(awk "BEGIN{printf \"%.2f\", $G2G_BEST/$HPSV_CPU}")" \
       "$(flag "$HPSV_CPU_OK")"
[ -n "$HPSV_GPU" ] && printf " hpsv CUDA                           : %8s s  [%s]  (%sx)%s\n" \
       "$HPSV_GPU" "$HPSV_GPU_SPREAD" "$(awk "BEGIN{printf \"%.2f\", $G2G_BEST/$HPSV_GPU}")" \
       "$(flag "$HPSV_GPU_OK")"
echo " hpsv builds from commit $CPU_COMMIT${GPU_COMMIT:+ / $GPU_COMMIT}"
echo "=============================================================="
echo
echo "Quote geo2grid's BEST time, not its default (4 workers) -- see 'Fairness'"
echo "in the header. Re-run on each host: these ratios do not transfer."
echo "Every timed run is in $RESULTS."
if [ "${KEEP:-0}" = "1" ]; then
    echo
    echo "Outputs kept in $WORKDIR. To check the two tools agree on the product:"
    echo "  reproduction/compare_g2g_product.sh $WORKDIR/g2g_best.tif $WORKDIR/hpsv_gpu.tif"
    echo "and the CPU route as well, since the two hpsv paths are not bit-identical:"
    echo "  reproduction/compare_g2g_product.sh $WORKDIR/g2g_best.tif $WORKDIR/hpsv_cpu.tif"
fi
