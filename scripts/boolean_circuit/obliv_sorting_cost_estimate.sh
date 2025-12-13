#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

OBLIV_SORT_N=128
UNWIND_OVERRIDE=""
NPARTS=$OBLIV_SORT_N
KAHIP_GAMMA=0.10
KAHIP_SEED=2
ELEMENT_BITWIDTH=32
OUTDIR=""
OUT_TAG=""
PY_ENV_PREFIX="${PY_ENV_PREFIX:-/tmp/python-venv/ARE_venv}"
ENV_SCRIPT="${REPO_ROOT}/scripts/local/env_bell_circuit"
REPORT_FILENAME="oblivious_sorting_stats.txt"

usage() {
  cat <<'EOF'
Usage: obliv_sorting_cost_estimate.sh [options]

Options:
  -n, --elements N      Number of uint32_t elements in the oblivious sort circuit (default: 128).
      --unwind K        Override loop unwind bound passed to cbmc-gc (default: 2*N).
  -p, --parts P         Number of partitions used for statistics (default: 8).
  -g, --gamma G         Imbalance tolerance passed to KaHIP (default: 0.10).
      --seed S          Seed for KaHIP (default: 2).
  -o, --outdir DIR      Output directory for circuit artifacts (default: build/boolean_circuits/bitonic_sort_u32_N{N}).
      --tag NAME        Base directory name under build/boolean_circuits (overrides default tag).
  -h, --help            Show this help message.

Environment:
  PY_ENV_PREFIX   Path to the conda environment prefix that hosts the Python deps (default: /tmp/python-venv/ARE_venv).

This script sources scripts/local/env_bell_circuit, runs the circuit generator,
and then evaluates the resulting circuit statistics using the Notebook template logic.
EOF
}

error() {
  echo "Error: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--elements)
      [[ $# -ge 2 ]] || error "--elements requires a value"
      OBLIV_SORT_N="$2"
      shift 2
      ;;
    --unwind)
      [[ $# -ge 2 ]] || error "--unwind requires a value"
      UNWIND_OVERRIDE="$2"
      shift 2
      ;;
    -p|--parts)
      [[ $# -ge 2 ]] || error "--parts requires a value"
      NPARTS="$2"
      shift 2
      ;;
    -g|--gamma)
      [[ $# -ge 2 ]] || error "--gamma requires a value"
      KAHIP_GAMMA="$2"
      shift 2
      ;;
    --seed)
      [[ $# -ge 2 ]] || error "--seed requires a value"
      KAHIP_SEED="$2"
      shift 2
      ;;
    -o|--outdir)
      [[ $# -ge 2 ]] || error "--outdir requires a value"
      OUTDIR="$2"
      shift 2
      ;;
    --tag)
      [[ $# -ge 2 ]] || error "--tag requires a value"
      OUT_TAG="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      error "Unknown option: $1"
      ;;
  esac
done

[[ "${OBLIV_SORT_N}" =~ ^[0-9]+$ ]] || error "elements must be a positive integer"
[[ "${NPARTS}" =~ ^[0-9]+$ ]]       || error "parts must be a positive integer"
[[ "${KAHIP_GAMMA}" =~ ^[0-9.]+$ ]] || error "gamma must be numeric"
[[ "${KAHIP_SEED}" =~ ^[0-9]+$ ]]   || error "seed must be a positive integer"
(( OBLIV_SORT_N > 0 )) || error "elements must be > 0"
(( NPARTS > 0 ))       || error "parts must be > 0"

UNWIND="${UNWIND_OVERRIDE}"
if [[ -z "${UNWIND}" ]]; then
  UNWIND=$(( OBLIV_SORT_N * 2 ))
fi

if [[ -z "${OUT_TAG}" ]]; then
  OUT_TAG="bitonic_sort_u32_N${OBLIV_SORT_N}"
fi

if [[ -z "${OUTDIR}" ]]; then
  OUTDIR="${REPO_ROOT}/build/boolean_circuits/${OUT_TAG}"
fi

CIRCUIT_NAME="$(basename "${OUTDIR}")"
GEN_SCRIPT="${REPO_ROOT}/src/boolean_circuit/oblivious_sort/gen_circuit_file.sh"
[[ -x "${GEN_SCRIPT}" ]] || error "Missing circuit generation script: ${GEN_SCRIPT}"

[[ -f "${ENV_SCRIPT}" ]] || error "Missing environment setup script: ${ENV_SCRIPT}"
echo "[1/3] Sourcing circuit toolchain environment from ${ENV_SCRIPT}"
# shellcheck disable=SC1090
source "${ENV_SCRIPT}"

echo "[2/3] Generating circuit for n=${OBLIV_SORT_N}, unwind=${UNWIND} -> ${OUTDIR}"
(
  cd "${REPO_ROOT}"
  "${GEN_SCRIPT}" -n "${OBLIV_SORT_N}" --unwind "${UNWIND}" --no-minimization -o "${OUTDIR}"
)

GATE_FILE="${OUTDIR}/output.gate.txt"
[[ -f "${GATE_FILE}" ]] || error "Circuit generation did not produce ${GATE_FILE}"

PYTHON_BIN="${PY_ENV_PREFIX}/bin/python"
[[ -x "${PYTHON_BIN}" ]] || error "Python environment not found at ${PYTHON_BIN}. Run scripts/local/install_cluster_bell_python_boundle.sh first."

export PYTHONPATH="${REPO_ROOT}/src:${PYTHONPATH:-}"
REPORT_PATH="${OUTDIR}/${REPORT_FILENAME}"

echo "[3/3] Computing circuit statistics with ${PYTHON_BIN}"
REPORT_TEXT="$(
  GATE_FILE="${GATE_FILE}" \
  NUM_PARTS="${NPARTS}" \
  INPUT_ELEMENTS="${OBLIV_SORT_N}" \
  ELEMENT_BITWIDTH="${ELEMENT_BITWIDTH}" \
  CIRCUIT_NAME="${CIRCUIT_NAME}" \
  KAHIP_GAMMA="${KAHIP_GAMMA}" \
  KAHIP_SEED="${KAHIP_SEED}" \
  "${PYTHON_BIN}" - <<'PY'
import os
import sys
import numpy as np

from boolean_circuit.graph_synthesizer import CircuitGraph
from boolean_circuit.partitioner import KaHIPPartitioner, BalancedContiguousPartitioner

gate_file = os.environ["GATE_FILE"]
nparts = int(os.environ["NUM_PARTS"])
num_inputs = int(os.environ["INPUT_ELEMENTS"])
elem_bits = int(os.environ["ELEMENT_BITWIDTH"])
circuit_name = os.environ["CIRCUIT_NAME"]
kahip_gamma = float(os.environ["KAHIP_GAMMA"])
kahip_seed = int(os.environ["KAHIP_SEED"])

cg = CircuitGraph.from_cbmc_gc_gate_file(gate_file)
pg = cg.to_partitionable()

part = None
partition_note = None

try:
    partitioner = KaHIPPartitioner(mode=0, seed=kahip_seed, suppress_output=1)
    part = partitioner.partition(pg, nparts=nparts, gamma=kahip_gamma)
except Exception as exc:
    partition_note = f"KaHIPPartitioner failed ({exc}); fell back to BalancedContiguousPartitioner."
    fallback = BalancedContiguousPartitioner()
    part = fallback.partition(pg, nparts=nparts, gamma=kahip_gamma)

metrics = pg.summary_metrics(part, nparts=nparts)

def _fmt(value):
    if isinstance(value, (np.generic,)):
        return int(value)
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value

lines = [
    f"Okay for the oblivious sorting circuit '{circuit_name}' the input number is {num_inputs} and each item uses {elem_bits}-bit words.",
    "Circuit statistics:",
    f"  non_xor_gates    : {_fmt(metrics['total_nonxor'])}",
    f"  max_in_boundary  : {_fmt(metrics['max_in_boundary'])}",
    f"  max_out_boundary : {_fmt(metrics['max_out_boundary'])}",
    f"  max_cross_boundary: {_fmt(metrics['max_cross_boundary'])}",
    f"  max_load         : {_fmt(metrics['max_load'])}",
]

if partition_note:
    lines.append("")
    lines.append(partition_note)

print("\n".join(lines))
PY
)"

echo "${REPORT_TEXT}"
printf '%s\n' "${REPORT_TEXT}" > "${REPORT_PATH}"
echo "Statistics saved to ${REPORT_PATH}"
