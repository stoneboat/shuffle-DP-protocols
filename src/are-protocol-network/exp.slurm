#!/bin/bash
#SBATCH --account=gts-vzikas3
#SBATCH --job-name=ARE-Shares
#SBATCH --partition=cpu-amd
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=24
#SBATCH --mem=600G
#SBATCH --time=24:00:00
#SBATCH --output=logs/are_shares_%A_%a.out
#SBATCH --error=logs/are_shares_%A_%a.err
#SBATCH --array=0-15

set -euo pipefail

# Array layout: 4 circuits × 4 partition modes = 16 tasks.
CIRCUITS=("gausssum" "select" "distinct" "lcb")
PARTITIONS=("topo_bal" "unbal" "nonxor_bal" "min_max_in")
SHARES="${SHARES:-2}"
OUTPUT_DIR="benchmark/csv_shares_${SHARES}"

CIRC_IDX=$(( SLURM_ARRAY_TASK_ID / ${#PARTITIONS[@]} ))
PART_IDX=$(( SLURM_ARRAY_TASK_ID % ${#PARTITIONS[@]} ))
CURRENT_CIRC="${CIRCUITS[$CIRC_IDX]}"
CURRENT_PART="${PARTITIONS[$PART_IDX]}"

# Navigate to project root and setup paths
cd "$SLURM_SUBMIT_DIR"
export LD_LIBRARY_PATH="$(pwd)/../utils/emp-tool/install/lib:${LD_LIBRARY_PATH:-}"
mkdir -p "$OUTPUT_DIR" logs

# Ensure the lookup tables are accessible (working dir + bin/)
cp bin/lookup_12.bin . 2>/dev/null || true

BASE_PORT=$(( 4000 + SLURM_ARRAY_TASK_ID * 1500 ))

# Hand all granted CPUs to OpenMP for parallel OT-ARE / PXT-ARE encoding.
export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-24}"

# Increase limits for client spawning (N up to 1024)
ulimit -u 8192
ulimit -n 8192

echo "Task ID: $SLURM_ARRAY_TASK_ID | Circuit: $CURRENT_CIRC | Partition: $CURRENT_PART"
echo "Node: $SLURMD_NODENAME | BasePort: $BASE_PORT | Threads: $OMP_NUM_THREADS"
echo "Shares per encoding: $SHARES | CSV directory: $OUTPUT_DIR | Source: $(git describe --always --dirty)"

EVAL_ARGS=(
    --benchmark --spawn-clients
    --circuit   "$CURRENT_CIRC"
    --partition "$CURRENT_PART"
    --n-min 8 --n-max 1024
    --K 8 --D 2 --NB 8
    --shares "$SHARES"
    --port "$BASE_PORT"
    --csv "$OUTPUT_DIR/audit_${CURRENT_CIRC}_${CURRENT_PART}.csv"
)
./bin/evaluator "${EVAL_ARGS[@]}"

echo "Done with $CURRENT_CIRC / $CURRENT_PART."
