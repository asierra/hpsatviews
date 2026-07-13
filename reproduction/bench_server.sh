#!/bin/bash
# Deployment benchmark: CPU build vs CUDA build on the target server, over the
# real workload (full-disk true color + Rayleigh, default GeoTIFF output).
#
# The dev-box speedups (RTX 5060 Ti vs 6 cores) do NOT transfer to other
# machines: a strong many-core CPU and the GPU's FP64 throughput / PCIe
# generation dominate. Always re-measure on the actual server before enabling
# --cuda in production.
#
# Usage:
#   reproduction/bench_server.sh <anchor_full_disk.nc>
#
# Env overrides (match your GPU and distro):
#   CUDA_ARCH   GPU arch: sm_75 (Tesla T4), sm_80 (A30/A100), sm_89 (RTX 40xx),
#               sm_86 (RTX 30xx/A10), sm_90 (H100), sm_120 (RTX 50xx). Default sm_80.
#   HDF5_LIB    HDF5 C lib name: hdf5_serial (Debian/Ubuntu) or hdf5 (RHEL/Rocky).
#               Left unset, the Makefile auto-detects it.
#   OMP_NUM_THREADS  Cap CPU threads to your production allocation (shared hosts).
#
# Run from the repo root. Rebuilds in each mode, times two runs and extracts the
# per-stage -v breakdown. Does not touch anything in production.
set -e
ANCHOR="${1:?Usage: $0 <anchor_full_disk.nc>}"
ARCH="${CUDA_ARCH:-sm_80}"
MK_HDF5=""
[ -n "$HDF5_LIB" ] && MK_HDF5="HDF5_LIB=$HDF5_LIB"
OUT="$(mktemp --suffix=.tif)"

run() { local t0 t1; t0=$(date +%s.%N); "$@" >/dev/null 2>&1; t1=$(date +%s.%N); \
        echo "$(echo "$t1 - $t0" | bc) s"; }

echo "== CPU threads: $(nproc) (OMP_NUM_THREADS=${OMP_NUM_THREADS:-all}) =="
echo "== GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo N/A) | CUDA_ARCH=$ARCH =="

echo ""
echo "### CPU build (no CUDA) ###"
make clean >/dev/null 2>&1 && make $MK_HDF5 >/dev/null 2>&1 && echo "build ok"
echo "-- per-stage (-v) --"
./bin/hpsv rgb "$ANCHOR" --mode truecolor --rayleigh -o "$OUT" -v 2>&1 \
  | grep -iE "chunked decompress|Rayleigh LUT \(|Solar geometry|Satellite geometry|GeoTIFF|Downsampling"
echo -n "wall CPU  #1: "; run ./bin/hpsv rgb "$ANCHOR" --mode truecolor --rayleigh -o "$OUT"
echo -n "wall CPU  #2: "; run ./bin/hpsv rgb "$ANCHOR" --mode truecolor --rayleigh -o "$OUT"

echo ""
echo "### CUDA build (CUDA_ARCH=$ARCH) ###"
make clean >/dev/null 2>&1 && make CUDA=1 CUDA_ARCH="$ARCH" $MK_HDF5 >/dev/null 2>&1 && echo "build ok"
echo "-- per-stage (-v): watch Navigation (double-heavy, GPU FP64-sensitive) --"
./bin/hpsv rgb "$ANCHOR" --mode truecolor --rayleigh --cuda -o "$OUT" -v 2>&1 \
  | grep -iE "chunked decompress|DataF upload|Navigation \(CUDA|Rayleigh LUT \(CUDA|Solar zenith|Synthetic green|Multiband|GeoTIFF|Downsampling"
echo -n "wall CUDA #1: "; run ./bin/hpsv rgb "$ANCHOR" --mode truecolor --rayleigh --cuda -o "$OUT"
echo -n "wall CUDA #2: "; run ./bin/hpsv rgb "$ANCHOR" --mode truecolor --rayleigh --cuda -o "$OUT"

rm -f "$OUT"
echo ""
echo "Enable --cuda in production only if the CUDA wall consistently beats the CPU"
echo "wall on THIS server under the real OMP_NUM_THREADS and GPU contention."
