#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

NPARTS=128
UNWIND_OVERRIDE=""
KAHIP_GAMMA=0.10
KAHIP_SEED=2
ELEMENT_BITWIDTH="${ELEMENT_BITWIDTH:-32}"
OUTDIR=""
OUT_TAG=""
PY_ENV_PREFIX="${PY_ENV_PREFIX:-/tmp/python-venv/ARE_venv}"
ENV_SCRIPT="${REPO_ROOT}/scripts/local/env_bell_circuit"
REPORT_FILENAME="oblivious_sorting_stats.txt"

usage() {
  cat <<'EOF'
Usage: obliv_sorting_cost_estimate.sh [options]

Options:
  -p, --parts P         Number of elements and partitions (default: 16).
      --unwind K        Override loop unwind bound passed to cbmc-gc (default: 2*P).
  -g, --gamma G         Imbalance tolerance passed to KaHIP (default: 0.10).
      --seed S          Seed for KaHIP (default: 2).
  -o, --outdir DIR      Output directory for circuit artifacts (default: build/boolean_circuits/oblivious_sort_u32_N{P}).
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
    -w|--bitwidth)
      [[ $# -ge 2 ]] || error "--bitwidth requires a value"
      ELEMENT_BITWIDTH="$2"
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

[[ "${NPARTS}" =~ ^[0-9]+$ ]]       || error "parts must be a positive integer"
[[ "${KAHIP_GAMMA}" =~ ^[0-9.]+$ ]] || error "gamma must be numeric"
[[ "${KAHIP_SEED}" =~ ^[0-9]+$ ]]   || error "seed must be a positive integer"
(( NPARTS > 0 ))       || error "parts must be > 0"

UNWIND="${UNWIND_OVERRIDE}"
if [[ -z "${UNWIND}" ]]; then
  UNWIND=$(( NPARTS * 2 ))
fi

if [[ -z "${OUT_TAG}" ]]; then
  OUT_TAG="oblivious_sort_u${ELEMENT_BITWIDTH}_N${NPARTS}"
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

echo "[2/3] Generating circuit for n=${NPARTS}, unwind=${UNWIND} -> ${OUTDIR}"
(
  cd "${REPO_ROOT}"
  OBLIV_SORT_W="${ELEMENT_BITWIDTH}" \
  "${GEN_SCRIPT}" -n "${NPARTS}" -w "${ELEMENT_BITWIDTH}" --unwind "${UNWIND}" --no-minimization -o "${OUTDIR}"
)

GATE_FILE="${OUTDIR}/output.gate.txt"
[[ -f "${GATE_FILE}" ]] || error "Circuit generation did not produce ${GATE_FILE}"

PYTHON_BIN="${PY_ENV_PREFIX}/bin/python"
[[ -x "${PYTHON_BIN}" ]] || error "Python environment not found at ${PYTHON_BIN}. Run scripts/local/install_cluster_bell_python_boundle.sh first."

export PYTHONPATH="${REPO_ROOT}/src:${PYTHONPATH:-}"
REPORT_PATH="${OUTDIR}/${REPORT_FILENAME}"

COMPUTE_STATS_SCRIPT="${SCRIPT_DIR}/compute_circuit_statistics.py"
[[ -f "${COMPUTE_STATS_SCRIPT}" ]] || error "Missing statistics script: ${COMPUTE_STATS_SCRIPT}"

echo "[3/3] Computing circuit statistics with ${PYTHON_BIN}"
REPORT_TEXT="$(
  GATE_FILE="${GATE_FILE}" \
  NUM_PARTS="${NPARTS}" \
  INPUT_ELEMENTS="${NPARTS}" \
  ELEMENT_BITWIDTH="${ELEMENT_BITWIDTH}" \
  CIRCUIT_NAME="${CIRCUIT_NAME}" \
  KAHIP_GAMMA="${KAHIP_GAMMA}" \
  KAHIP_SEED="${KAHIP_SEED}" \
  "${PYTHON_BIN}" "${COMPUTE_STATS_SCRIPT}"
)"

echo "${REPORT_TEXT}"
printf '%s\n' "${REPORT_TEXT}" > "${REPORT_PATH}"
echo "Statistics saved to ${REPORT_PATH}"
