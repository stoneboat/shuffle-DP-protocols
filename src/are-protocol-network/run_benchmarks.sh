#!/bin/bash
# Run networked protocol benchmarks for one (circuit, partition_mode) pair.
# Designed to be invoked once per slurm task — the heavy circuits (lcb, distinct)
# are split across 4 partition_mode jobs each so the total wall clock fits.
#
# Usage:
#   bash run_benchmarks.sh CIRCUIT PARTITION_MODE [N_MIN] [N_MAX] [BASE_PORT]
#
# Examples:
#   bash run_benchmarks.sh lcb topo_bal      8 512 20500
#   bash run_benchmarks.sh distinct min_max_in 8 512 21000
#
# CIRCUIT        ∈ {gausssum, select, lcb, distinct, bitonic, treemech, ...}
# PARTITION_MODE ∈ {topo_bal, unbal, nonxor_bal, min_cut, min_max_in}
#
# Each invocation writes its own CSV so parallel slurm tasks don't clobber:
#   benchmark/csv/audit_<circuit>_<partition>.csv
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CIRCUIT="${1:?usage: run_benchmarks.sh CIRCUIT PARTITION_MODE [N_MIN] [N_MAX] [BASE_PORT]}"
PARTITION="${2:?usage: run_benchmarks.sh CIRCUIT PARTITION_MODE [N_MIN] [N_MAX] [BASE_PORT]}"
N_MIN="${3:-8}"
N_MAX="${4:-512}"
BASE_PORT="${5:-20500}"

OUTPUT_DIR="benchmark/csv"
mkdir -p "$OUTPUT_DIR"
CSV="${OUTPUT_DIR}/audit_${CIRCUIT}_${PARTITION}.csv"

# Use all cores granted by the scheduler for OpenMP-parallel encoding loops.
# SLURM_CPUS_PER_TASK is set by `--cpus-per-task`; fall back to nproc otherwise.
export OMP_NUM_THREADS="${OMP_NUM_THREADS:-${SLURM_CPUS_PER_TASK:-$(nproc)}}"

echo "=== run_benchmarks: circuit=$CIRCUIT partition=$PARTITION N=[$N_MIN..$N_MAX] port=$BASE_PORT threads=$OMP_NUM_THREADS ==="
./bin/evaluator --benchmark --spawn-clients \
    --circuit "$CIRCUIT" \
    --partition "$PARTITION" \
    --n-min "$N_MIN" --n-max "$N_MAX" \
    --port "$BASE_PORT" \
    -o "$CSV"
echo "Done. Results in $CSV"