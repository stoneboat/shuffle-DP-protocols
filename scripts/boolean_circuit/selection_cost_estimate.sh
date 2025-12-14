#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

NPARTS=256
NUM_RECORDS=256
NUM_CHOICES=16
FRAC_BITS=32
UNWIND_OVERRIDE=""
KAHIP_GAMMA=0.10
KAHIP_SEED=2
OUTDIR=""
OUT_TAG=""
PY_ENV_PREFIX="${PY_ENV_PREFIX:-/tmp/python-venv/ARE_venv}"
ENV_SCRIPT="${REPO_ROOT}/scripts/local/env_bell_circuit"
REPORT_FILENAME="dp_selection_gumbel_stats.txt"
CIRCUIT_NAME="dp_selection_gumbel"

usage() {
  cat <<'EOF'
Usage: selection_cost_estimate.sh [options]

Options:
  -n, --num-records N   Number of records in the histogram (default: 8).
  -d, --num-choices D   Number of categories/columns per record (default: 8).
  -f, --frac-bits B     Fixed-point fractional bits for the Gumbel noise (default: 32).
  -p, --parts P         Number of partitions for KaHIP (default: 8).
      --unwind K        Override loop unwind bound passed to cbmc-gc (default: N*D+1).
  -g, --gamma G         Imbalance tolerance passed to KaHIP (default: 0.10).
      --seed S          Seed for KaHIP (default: 2).
  -o, --outdir DIR      Output directory for circuit artifacts (default: build/boolean_circuits/dp_selection_gumbel_u1_N{N}_D{D}).
      --tag NAME        Base directory name under build/boolean_circuits (overrides default tag).
  -h, --help            Show this help message.

Environment:
  PY_ENV_PREFIX   Path to the conda environment prefix that hosts the Python deps (default: /tmp/python-venv/ARE_venv).

This script sources scripts/local/env_bell_circuit, runs the dp_selection_gumbel circuit generator,
and then evaluates the resulting circuit statistics using the Notebook template logic.
EOF
}

error() {
  echo "Error: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--num-records)
      [[ $# -ge 2 ]] || error "--num-records requires a value"
      NUM_RECORDS="$2"
      shift 2
      ;;
    -d|--num-choices)
      [[ $# -ge 2 ]] || error "--num-choices requires a value"
      NUM_CHOICES="$2"
      shift 2
      ;;
    -f|--frac-bits)
      [[ $# -ge 2 ]] || error "--frac-bits requires a value"
      FRAC_BITS="$2"
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

[[ "${NPARTS}" =~ ^[0-9]+$ ]]       || error "parts must be a positive integer"
[[ "${NUM_RECORDS}" =~ ^[0-9]+$ ]]  || error "num-records must be a positive integer"
[[ "${NUM_CHOICES}" =~ ^[0-9]+$ ]]  || error "num-choices must be a positive integer"
[[ "${FRAC_BITS}" =~ ^[0-9]+$ ]]    || error "frac-bits must be a non-negative integer"
[[ "${KAHIP_GAMMA}" =~ ^[0-9.]+$ ]] || error "gamma must be numeric"
[[ "${KAHIP_SEED}" =~ ^[0-9]+$ ]]   || error "seed must be a positive integer"
(( NPARTS > 0 ))       || error "parts must be > 0"
(( NUM_RECORDS > 0 ))  || error "num-records must be > 0"
(( NUM_CHOICES > 0 ))  || error "num-choices must be > 0"

UNWIND="${UNWIND_OVERRIDE}"
if [[ -z "${UNWIND}" ]]; then
  UNWIND=$(( NUM_RECORDS * NUM_CHOICES + 1 ))
fi

if [[ -z "${OUT_TAG}" ]]; then
  OUT_TAG="${CIRCUIT_NAME}_u32_N${NUM_RECORDS}_D${NUM_CHOICES}"
fi

if [[ -z "${OUTDIR}" ]]; then
  OUTDIR="${REPO_ROOT}/build/boolean_circuits/${OUT_TAG}"
fi

GEN_SCRIPT="${REPO_ROOT}/src/boolean_circuit/selection/gen_circuit_file.sh"
[[ -x "${GEN_SCRIPT}" ]] || error "Missing circuit generation script: ${GEN_SCRIPT}"

[[ -f "${ENV_SCRIPT}" ]] || error "Missing environment setup script: ${ENV_SCRIPT}"
echo "[1/3] Sourcing circuit toolchain environment from ${ENV_SCRIPT}"
# shellcheck disable=SC1090
source "${ENV_SCRIPT}"

echo "[2/3] Generating circuit for n=${NUM_RECORDS}, d=${NUM_CHOICES}, frac_bits=${FRAC_BITS}, unwind=${UNWIND} -> ${OUTDIR}"
(
  cd "${REPO_ROOT}"
  "${GEN_SCRIPT}" \
    -n "${NUM_RECORDS}" \
    -d "${NUM_CHOICES}" \
    -f "${FRAC_BITS}" \
    --parts "${NPARTS}" \
    --unwind "${UNWIND}" \
    --no-minimization \
    -o "${OUTDIR}"
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
  INPUT_ELEMENTS="${NUM_RECORDS}" \
  ELEMENT_BITWIDTH="${NUM_CHOICES}" \
  CIRCUIT_NAME="${CIRCUIT_NAME}" \
  KAHIP_GAMMA="${KAHIP_GAMMA}" \
  KAHIP_SEED="${KAHIP_SEED}" \
  "${PYTHON_BIN}" "${COMPUTE_STATS_SCRIPT}"
)"

echo "${REPORT_TEXT}"
printf '%s\n' "${REPORT_TEXT}" > "${REPORT_PATH}"
echo "Statistics saved to ${REPORT_PATH}"
