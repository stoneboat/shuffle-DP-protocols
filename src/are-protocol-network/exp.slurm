#!/bin/bash
#SBATCH --account=account_id
#SBATCH --job-name=ARE-Bench
#SBATCH --partition=cpu-amd
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=24
#SBATCH --mem=300G
#SBATCH --time=24:00:00
#SBATCH --output=logs/are_%A_%a.out
#SBATCH --error=logs/are_%A_%a.err
#SBATCH --array=0-15

# Array layout: 4 circuits × 4 partition modes = 16 tasks.
CIRCUITS=("gausssum" "select" "distinct" "lcb")
PARTITIONS=("topo_bal" "unbal" "nonxor_bal" "min_max_in")

CIRC_IDX=$(( SLURM_ARRAY_TASK_ID / ${#PARTITIONS[@]} ))
PART_IDX=$(( SLURM_ARRAY_TASK_ID % ${#PARTITIONS[@]} ))
CURRENT_CIRC="${CIRCUITS[$CIRC_IDX]}"
CURRENT_PART="${PARTITIONS[$PART_IDX]}"

# Navigate to project root and setup paths
cd "$SLURM_SUBMIT_DIR"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$(pwd)/../utils/emp-tool/install/lib"
mkdir -p benchmark/csv logs

# Ensure the lookup tables are accessible (working dir + bin/)
cp bin/lookup_12.bin . 2>/dev/null || true

BASE_PORT=$(( 20000 + SLURM_ARRAY_TASK_ID * 600 ))

# Hand all granted CPUs to OpenMP for parallel OT-ARE / PXT-ARE encoding.
export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-24}"

# Increase limits for client spawning (N up to 512)
ulimit -u 8192
ulimit -n 8192

echo "Task ID: $SLURM_ARRAY_TASK_ID | Circuit: $CURRENT_CIRC | Partition: $CURRENT_PART"
echo "Node: $SLURMD_NODENAME | BasePort: $BASE_PORT | Threads: $OMP_NUM_THREADS"

EVAL_ARGS=(
    --benchmark --spawn-clients
    --circuit   "$CURRENT_CIRC"
    --partition "$CURRENT_PART"
    --n-min 8 --n-max 512
    --port "$BASE_PORT"
    --csv "benchmark/csv/audit_${CURRENT_CIRC}_${CURRENT_PART}.csv"
)
./bin/evaluator "${EVAL_ARGS[@]}"

echo "Done with $CURRENT_CIRC / $CURRENT_PART."