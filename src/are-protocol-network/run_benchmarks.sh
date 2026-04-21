#!/bin/bash
# Run networked protocol benchmarks for all circuit/N/balanced combos.
# Writes directly to one CSV file — no individual files generated.
#
# Uses the evaluator's built-in --benchmark mode which loops through
# all combinations and writes a single CSV at the end.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

OUTPUT_DIR="benchmark"
mkdir -p "$OUTPUT_DIR"

CSV="${OUTPUT_DIR}/all_results.csv"

echo "=== Running all benchmarks ==="
./bin/evaluator --benchmark --spawn-clients -o "$CSV"
echo "Done. Results in $CSV"
