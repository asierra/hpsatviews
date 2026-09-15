#!/bin/bash
# Time budget of one full-disk render: where the wall time goes, per build.
#
# Produces the per-stage record that answers "once the arithmetic is on an
# accelerator, what is the remaining time actually spent on?" — startup, input
# (read + decode + unpack), compute, output. The paper reports the endpoints of
# that budget; this script produces the breakdown behind them.
#
# Both builds are compiled from the SAME working tree and timed in the SAME
# session, which is the first of the three measurement failure modes described
# in the paper: a CPU number from an older revision inflates the GPU speed-up.
#
# Usage:
#   reproduction/bench_timebudget.sh <anchor_full_disk.nc> [out.csv]
#
# Env overrides:
#   CUDA_ARCH   sm_75 (Tesla T4), sm_80 (A30/A100, default), sm_86, sm_89, sm_90,
#               sm_120 (RTX 50xx).
#   HDF5_LIB    hdf5_serial (Debian/Ubuntu) or hdf5 (RHEL/Rocky). Unset =
#               Makefile auto-detects.
#   REPS        timed runs per build (default 3; the summary takes the median).
#   OMP_NUM_THREADS  cap CPU threads to the production allocation.
#   SKIP_CUDA=1 CPU build only (host without a GPU).
#   BENCH_BIN   where each build is copied and timed from (default bench_bin/,
#               ignored by git): hpsv-openmp and hpsv-cuda. bin/ cannot hold
#               both, because 'make clean' removes it and the two builds share
#               obj/. bench_geo2grid.sh reads them from here, so its CPU row is
#               the build without CUDA and both rows come from this revision.
#   EXTRA_ARGS  extra hpsv flags, so the budget can be measured on the SAME
#               product a paper reports rather than on the bare composite:
#                   EXTRA_ARGS="--full-res --sharpen --stretch"
#               --full-res is the one that sets the size, and it is easy to
#               forget: without it the reference channel is the LOWEST-resolution
#               one loaded (1 km, 10848x10848), while the manuscript's product is
#               the sharpened composite at C02's native 0.5 km (21696x21696),
#               four times the pixels. --sharpen and --stretch change the work
#               but not the size, so a run missing only --full-res still looks
#               plausible: check nx/ny in the CSV, not just the flags.
#               A budget measured on the wrong product does not add up to the
#               wall time in the results table, and the mismatch is invisible in
#               the figure — it just looks like a different machine.
#               At 0.5 km each float grid is 1.88 GB; the temporary GeoTIFF is
#               written under TMPDIR, so point TMPDIR at real disk if /tmp is a
#               small tmpfs.
#
# Every timed run is also timed from outside the process, and that external wall
# time goes to <out>_wall.csv, keyed by the run's t_start_utc. The CSV's own
# t_total is a narrower clock: it starts once the configuration is parsed and
# stops when the row is written, so it leaves out loading the executable and its
# libraries, and releasing memory and the CUDA context at exit. That is the
# right total for the budget, whose stages live inside it, but not for a wall
# time quoted against another tool timed from outside. timebudget_summary.py
# reads the wall file automatically and reports the difference.
#
# Run from the repo root. Rebuilds in each mode; touches nothing in production.
set -e

ANCHOR="${1:?Usage: $0 <anchor_full_disk.nc> [out.csv]}"
[ -r "$ANCHOR" ] || { echo "No such input: $ANCHOR" >&2; exit 1; }
CSV="${2:-timebudget_$(hostname -s)_$(date +%Y%m%d).csv}"
WALL="${CSV%.csv}_wall.csv"
BENCH_BIN="${BENCH_BIN:-bench_bin}"
mkdir -p "$BENCH_BIN"
ARCH="${CUDA_ARCH:-sm_80}"
REPS="${REPS:-3}"
MK_HDF5=""
[ -n "$HDF5_LIB" ] && MK_HDF5="HDF5_LIB=$HDF5_LIB"
OUT="$(mktemp --suffix=.tif)"
trap 'rm -f "$OUT"' EXIT

# "Same revision" is the whole point; say so if the tree is dirty.
if git -C . rev-parse --git-dir >/dev/null 2>&1; then
  COMMIT="$(git rev-parse --short HEAD)"
  if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "WARNING: working tree is dirty. Both builds will share these edits," >&2
    echo "         but the recorded commit ($COMMIT) will not describe them." >&2
  fi
  echo "== revision: $COMMIT =="
fi

echo "== host: $(hostname -s) | CPU threads: $(nproc) (OMP_NUM_THREADS=${OMP_NUM_THREADS:-all}) =="
echo "== GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo N/A) =="
echo "== input: $ANCHOR =="
echo "== product: truecolor --rayleigh ${EXTRA_ARGS:-(no extra flags)} =="
echo "== CSV: $CSV (rows are appended; $REPS timed runs per build) =="
echo "== external wall times: $WALL =="
[ -s "$WALL" ] || echo "t_start_utc,label,wall_s" > "$WALL"

# shellcheck disable=SC2206  # word splitting of EXTRA_ARGS is the point
ARGS=(rgb "$ANCHOR" --mode truecolor --rayleigh ${EXTRA_ARGS:-} -o "$OUT")

data_rows() {         # data rows in the record, header and schema line excluded
  [ -f "$CSV" ] || { echo 0; return; }
  grep -v -e '^#' -e '^t_start_utc' "$CSV" | wc -l
}

measure() {           # measure <label> <binary> <extra hpsv args...>
  local label="$1" bin="$2"; shift 2
  local before s e
  echo "-- $label: warm-up (discarded, warms the page cache) --"
  "$bin" "${ARGS[@]}" "$@" >/dev/null 2>&1
  for i in $(seq 1 "$REPS"); do
    echo "-- $label: timed run $i/$REPS --"
    before=$(data_rows)
    s=$(date +%s.%N)
    "$bin" "${ARGS[@]}" "$@" --timing-csv "$CSV" >/dev/null 2>&1
    e=$(date +%s.%N)
    # Pair the external time with the row this run appended. If the run left
    # no row (hpsv diverted it to a file of another schema), say so rather than
    # attach the time to someone else's row.
    if [ "$(data_rows)" -eq $((before + 1)) ]; then
      printf '%s,%s,%s\n' "$(tail -n 1 "$CSV" | cut -d, -f1)" "$label" \
        "$(echo "$e $s" | awk '{printf "%.3f", $1-$2}')" >> "$WALL"
    else
      echo "WARNING: run $i left no row in $CSV; its wall time is not recorded." >&2
    fi
  done
}

echo ""
echo "### CPU build (OpenMP) ###"
make clean >/dev/null 2>&1
make $MK_HDF5 >/dev/null 2>&1 && echo "build ok"
cp bin/hpsv "$BENCH_BIN/hpsv-openmp"
measure "openmp" "$BENCH_BIN/hpsv-openmp"

if [ "${SKIP_CUDA:-0}" != "1" ]; then
  echo ""
  echo "### CUDA build (CUDA_ARCH=$ARCH) ###"
  # A CFLAGS-only change does not rebuild the C objects; the clean is required.
  make clean >/dev/null 2>&1
  make CUDA=1 CUDA_ARCH="$ARCH" $MK_HDF5 >/dev/null 2>&1 && echo "build ok"
  ldd bin/hpsv | grep -q cudart && echo "linked against cudart: yes" \
    || echo "WARNING: binary is not linked against cudart; --cuda will fall back" >&2
  cp bin/hpsv "$BENCH_BIN/hpsv-cuda"
  measure "cuda" "$BENCH_BIN/hpsv-cuda" --cuda
fi

echo ""
python3 "$(dirname "$0")/timebudget_summary.py" "$CSV"

echo "Builds kept for bench_geo2grid.sh: $BENCH_BIN/hpsv-openmp$([ "${SKIP_CUDA:-0}" != "1" ] && echo " and $BENCH_BIN/hpsv-cuda")"

# The tree is left in whichever mode was built last. Say so: anything running
# from this checkout's bin/hpsv (rather than an installed copy) is now using a
# different build than before, and was briefly missing during each make clean.
echo ""
if [ "${SKIP_CUDA:-0}" != "1" ]; then
  echo "NOTE: bin/hpsv is now the CUDA build (CUDA_ARCH=$ARCH), and was rebuilt"
  echo "      twice during this run. If anything on this host runs from this"
  echo "      checkout rather than an installed copy, restore it with:"
  echo "          make clean && make $MK_HDF5"
else
  echo "NOTE: bin/hpsv was rebuilt (CPU build)."
fi
