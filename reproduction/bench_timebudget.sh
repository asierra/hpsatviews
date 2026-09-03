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
#
# Run from the repo root. Rebuilds in each mode; touches nothing in production.
set -e

ANCHOR="${1:?Usage: $0 <anchor_full_disk.nc> [out.csv]}"
[ -r "$ANCHOR" ] || { echo "No such input: $ANCHOR" >&2; exit 1; }
CSV="${2:-timebudget_$(hostname -s)_$(date +%Y%m%d).csv}"
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
echo "== CSV: $CSV (rows are appended; $REPS timed runs per build) =="

ARGS=(rgb "$ANCHOR" --mode truecolor --rayleigh -o "$OUT")

measure() {           # measure <label> <extra hpsv args...>
  local label="$1"; shift
  echo "-- $label: warm-up (discarded, warms the page cache) --"
  ./bin/hpsv "${ARGS[@]}" "$@" >/dev/null 2>&1
  for i in $(seq 1 "$REPS"); do
    echo "-- $label: timed run $i/$REPS --"
    ./bin/hpsv "${ARGS[@]}" "$@" --timing-csv "$CSV" >/dev/null 2>&1
  done
}

echo ""
echo "### CPU build (OpenMP) ###"
make clean >/dev/null 2>&1
make $MK_HDF5 >/dev/null 2>&1 && echo "build ok"
measure "openmp"

if [ "${SKIP_CUDA:-0}" != "1" ]; then
  echo ""
  echo "### CUDA build (CUDA_ARCH=$ARCH) ###"
  # A CFLAGS-only change does not rebuild the C objects; the clean is required.
  make clean >/dev/null 2>&1
  make CUDA=1 CUDA_ARCH="$ARCH" $MK_HDF5 >/dev/null 2>&1 && echo "build ok"
  ldd bin/hpsv | grep -q cudart && echo "linked against cudart: yes" \
    || echo "WARNING: binary is not linked against cudart; --cuda will fall back" >&2
  measure "cuda" --cuda
fi

echo ""
python3 "$(dirname "$0")/timebudget_summary.py" "$CSV"

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
