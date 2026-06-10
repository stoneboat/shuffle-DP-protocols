#!/bin/bash
# Launch evaluator + N clients on localhost
#
# Usage:
#   bash launch.sh N [PORT] [CIRCUIT] [extra evaluator args...]
#
# Examples:
#   bash launch.sh 4                          # 4 clients, default treemech
#   bash launch.sh 4 12345 treemech           # explicit port and circuit
#   bash launch.sh 8 12345 seqsum --K 8       # 8 clients, sequential sum, 8-bit
#   bash launch.sh 4 12345 treemech -o results.csv

set -euo pipefail

N=${1:?Usage: launch.sh N [PORT] [CIRCUIT] [args...]}
PORT=${2:-12345}
CIRCUIT=${3:-treemech}
shift 3 2>/dev/null || true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EVALUATOR="${SCRIPT_DIR}/evaluator"
CLIENT="${SCRIPT_DIR}/client"

[[ -x "$EVALUATOR" ]] || { echo "Error: $EVALUATOR not found. Run 'make' first."; exit 1; }
[[ -x "$CLIENT" ]]    || { echo "Error: $CLIENT not found. Run 'make' first."; exit 1; }

echo "Launching evaluator with $N clients on port $PORT, circuit=$CIRCUIT"

# Start evaluator in background
"$EVALUATOR" --clients "$N" --port "$PORT" --circuit "$CIRCUIT" "$@" &
EVAL_PID=$!

# Give the evaluator time to start listening
sleep 1

# Start clients
PIDS=()
for i in $(seq 0 $((N-1))); do
    "$CLIENT" --host 127.0.0.1 --port "$PORT" --id "$i" &
    PIDS+=($!)
done

# Wait for all to finish
wait "$EVAL_PID"
EVAL_EXIT=$?

for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
done

exit $EVAL_EXIT
