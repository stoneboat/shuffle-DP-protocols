#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

NPARTS=256
NUM_STEPS=256
VEC_DIM=1
LOG_STEPS=""
FRAC_BITS=32
UNWIND_OVERRIDE=""
KAHIP_GAMMA=0.10
KAHIP_SEED=2
OUTDIR=""
OUT_TAG=""
PY_ENV_PREFIX="${PY_ENV_PREFIX:-/tmp/python-venv/ARE_venv}"
ENV_SCRIPT="${REPO_ROOT}/scripts/local/env_bell_circuit"
REPORT_FILENAME="tree_mechanism_stats.txt"
CIRCUIT_NAME="tree_mechanism"

usage() {
  cat <<'EOF'
Usage: tree_mech_cost_estimate.sh [options]

Options:
  -t, --num-steps T     Number of time steps/releases NUM_STEPS (default: 8).
  -d, --vec-dim D       Length of each released vector VEC_DIM (default: 8).
  -l, --log-steps L     Fenwick-hop bound LOG_STEPS (default: computed as ceil(log2(T))+1).
  -f, --frac-bits B     Fixed-point fractional bits (default: 32).
  -p, --parts P         Number of partitions for KaHIP/balancer (default: 128).
      --unwind K        Override loop unwind bound passed to cbmc-gc (default: max(T*D, T+1, T, D, L)+1).
  -g, --gamma G         Imbalance tolerance passed to KaHIP (default: 0.10).
      --seed S          Seed for KaHIP (default: 2).
  -o, --outdir DIR      Output directory for circuit artifacts (default: build/boolean_circuits/tree_mechanism_T{T}_D{D}_L{L}).
      --tag NAME        Base directory name under build/boolean_circuits (overrides default tag).
  -h, --help            Show this help message.

Environment:
  PY_ENV_PREFIX   Path to the conda environment prefix that hosts the Python deps (default: /tmp/python-venv/ARE_venv).

This script sources scripts/local/env_bell_circuit, runs the tree_mechanism circuit generator,
and then evaluates the resulting circuit statistics using the Notebook template logic.
EOF
}

error() {
  echo "Error: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--num-steps|-n|--num-records)
      [[ $# -ge 2 ]] || error "--num-steps requires a value"
      NUM_STEPS="$2"
      shift 2
      ;;
    -d|--vec-dim|--num-choices)
      [[ $# -ge 2 ]] || error "--vec-dim requires a value"
      VEC_DIM="$2"
      shift 2
      ;;
    -l|--log-steps)
      [[ $# -ge 2 ]] || error "--log-steps requires a value"
      LOG_STEPS="$2"
      shift 2
      ;;
    -f|--frac-bits)
      [[ $# -ge 2 ]] || error "--frac-bits requires a value"
      FRAC_BITS="$2"
      shift 2
      ;;
    -p|--parts)
      [[ $# -ge 2 ]] || error "--parts requires a value"
      NPARTS="$2"
      shift 2
      ;;
    --unwind)
      [[ $# -ge 2 ]] || error "--unwind requires a value"
      UNWIND_OVERRIDE="$2"
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
[[ "${NUM_STEPS}" =~ ^[0-9]+$ ]]    || error "num-steps must be a positive integer"
[[ "${VEC_DIM}" =~ ^[0-9]+$ ]]      || error "vec-dim must be a positive integer"
[[ "${FRAC_BITS}" =~ ^[0-9]+$ ]]    || error "frac-bits must be a non-negative integer"
[[ "${KAHIP_GAMMA}" =~ ^[0-9.]+$ ]] || error "gamma must be numeric"
[[ "${KAHIP_SEED}" =~ ^[0-9]+$ ]]   || error "seed must be a positive integer"
(( NPARTS > 0 ))       || error "parts must be > 0"
(( NUM_STEPS > 0 ))    || error "num-steps must be > 0"
(( VEC_DIM > 0 ))      || error "vec-dim must be > 0"

# Compute LOG_STEPS from NUM_STEPS if not provided: ceil(log2(NUM_STEPS)) + 1
if [[ -z "${LOG_STEPS}" ]]; then
  # Calculate ceil(log2(NUM_STEPS)) + 1
  # Find the smallest power of 2 >= NUM_STEPS, then add 1
  log_val=0
  power=1
  while (( power < NUM_STEPS )); do
    log_val=$(( log_val + 1 ))
    power=$(( power * 2 ))
  done
  LOG_STEPS=$(( log_val + 1 ))
else
  [[ "${LOG_STEPS}" =~ ^[0-9]+$ ]]    || error "log-steps must be a positive integer"
  (( LOG_STEPS > 0 ))    || error "log-steps must be > 0"
fi

UNWIND="${UNWIND_OVERRIDE}"
if [[ -z "${UNWIND}" ]]; then
  UNWIND=$(( NUM_STEPS * VEC_DIM * LOG_STEPS + 1 ))
fi

if [[ -z "${OUT_TAG}" ]]; then
  OUT_TAG="${CIRCUIT_NAME}_T${NUM_STEPS}_D${VEC_DIM}_L${LOG_STEPS}"
fi

if [[ -z "${OUTDIR}" ]]; then
  OUTDIR="${REPO_ROOT}/build/boolean_circuits/${OUT_TAG}"
fi

GEN_SCRIPT="${REPO_ROOT}/src/boolean_circuit/tree_mechanism/gen_circuit_file.sh"
[[ -x "${GEN_SCRIPT}" ]] || error "Missing circuit generation script: ${GEN_SCRIPT}"

[[ -f "${ENV_SCRIPT}" ]] || error "Missing environment setup script: ${ENV_SCRIPT}"
echo "[1/3] Sourcing circuit toolchain environment from ${ENV_SCRIPT}"
# shellcheck disable=SC1090
source "${ENV_SCRIPT}"

echo "[2/3] Generating circuit for steps=${NUM_STEPS}, vec_dim=${VEC_DIM}, log_steps=${LOG_STEPS}, frac_bits=${FRAC_BITS}, parts=${NPARTS}, unwind=${UNWIND} -> ${OUTDIR}"
(
  cd "${REPO_ROOT}"
  "${GEN_SCRIPT}" \
    -t "${NUM_STEPS}" \
    -d "${VEC_DIM}" \
    -l "${LOG_STEPS}" \
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
  INPUT_ELEMENTS="${NUM_STEPS}" \
  ELEMENT_BITWIDTH="${VEC_DIM}" \
  CIRCUIT_NAME="${CIRCUIT_NAME}" \
  KAHIP_GAMMA="${KAHIP_GAMMA}" \
  KAHIP_SEED="${KAHIP_SEED}" \
  "${PYTHON_BIN}" "${COMPUTE_STATS_SCRIPT}"
)"

echo "${REPORT_TEXT}"
printf '%s\n' "${REPORT_TEXT}" > "${REPORT_PATH}"
echo "Statistics saved to ${REPORT_PATH}"
