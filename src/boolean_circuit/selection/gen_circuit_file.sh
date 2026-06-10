#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

DEFAULT_CBMC_GC_BIN="/tmp/are_env/CBMC-GC-2/build/bin/cbmc-gc"
CBMC_GC_BIN="${CBMC_GC_BIN:-${DEFAULT_CBMC_GC_BIN}}"
OUTDIR="${OUTDIR:-${REPO_ROOT}/build/boolean_circuits/dp_select_obliv_gumbel}"
NUM_RECORDS="${NUM_RECORDS:-8}"
NUM_CHOICES="${NUM_CHOICES:-16}"
FRAC_BITS="${FRAC_BITS:-32}"
NPARTS="${NPARTS:-8}"
UNWIND="${UNWIND:-128}"
NO_MINIMIZATION="${NO_MINIMIZATION:-false}"
MINIMIZATION_TIMEOUT_MINUTES="${MINIMIZATION_TIMEOUT_MINUTES:-10}"
EXTRA_CBMC_ARGS=()

usage() {
  cat <<'EOF'
Usage: gen_circuit_file.sh [options] [-- cbmc_args...]

Options:
  -n, --num-records N  Number of rows (each row holds d bits) aggregated in the histogram (default: 8).
  -d, --num-choices D  Number of categories/columns (default: 16).
  -f, --frac-bits B    Fixed-point fractional bits used for the gumbel noise (default: 32).
  -o, --outdir DIR     Output directory for the generated circuit (default: build/boolean_circuits/dp_select_obliv_gumbel).
  -p, --parts P        Define NPARTS macro (default: 8).
      --cbmc-gc PATH   Path to the cbmc-gc binary (default: /tmp/are_env/CBMC-GC-2/build/bin/cbmc-gc).
      --unwind K       Loop unwind bound passed to cbmc-gc (default: 128).
      --no-minimization Skip circuit minimization and SAT-based equivalence check (faster, but larger circuits).
      --min-timeout M  Limit minimization time to M minutes (default: 10 minutes).
  -h, --help           Show this message.

You can pass additional cbmc-gc flags after a literal "--".
Environment overrides: CBMC_GC_BIN, OUTDIR, NUM_RECORDS, NUM_CHOICES, FRAC_BITS, NPARTS, UNWIND.
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
    -p|--parts)
      [[ $# -ge 2 ]] || error "--parts requires a value"
      NPARTS="$2"
      shift 2
      ;;
    -o|--outdir)
      [[ $# -ge 2 ]] || error "--outdir requires a value"
      OUTDIR="$2"
      shift 2
      ;;
    --cbmc-gc)
      [[ $# -ge 2 ]] || error "--cbmc-gc requires a path"
      CBMC_GC_BIN="$2"
      shift 2
      ;;
    --unwind)
      [[ $# -ge 2 ]] || error "--unwind requires a value"
      UNWIND="$2"
      shift 2
      ;;
    --no-minimization)
      NO_MINIMIZATION="true"
      shift
      ;;
    --min-timeout)
      [[ $# -ge 2 ]] || error "--min-timeout requires a value (in minutes)"
      MINIMIZATION_TIMEOUT_MINUTES="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      EXTRA_CBMC_ARGS=("$@")
      break
      ;;
    *)
      error "Unknown option: $1"
      ;;
  esac
done

[[ -x "${CBMC_GC_BIN}" ]] || error "cbmc-gc binary not found or not executable: ${CBMC_GC_BIN}"
[[ "${NUM_RECORDS}" =~ ^[0-9]+$ ]] || error "NUM_RECORDS must be a positive integer"
[[ "${NUM_CHOICES}" =~ ^[0-9]+$ ]] || error "NUM_CHOICES must be a positive integer"
[[ "${FRAC_BITS}" =~ ^[0-9]+$ ]] || error "FRAC_BITS must be a non-negative integer"
[[ "${NPARTS}" =~ ^[0-9]+$ ]] || error "NPARTS must be a positive integer"
(( NUM_RECORDS > 0 )) || error "NUM_RECORDS must be greater than zero"
(( NUM_CHOICES > 0 )) || error "NUM_CHOICES must be greater than zero"
(( NPARTS > 0 )) || error "NPARTS must be greater than zero"

TOTAL_BITS=$((NUM_RECORDS * NUM_CHOICES))
(( TOTAL_BITS > 0 )) || error "Product NUM_RECORDS * NUM_CHOICES must fit in 64-bit shell arithmetic"

mkdir -p "${OUTDIR}"

HARNESS="${SCRIPT_DIR}/dp_select_obliv_gumbel_harness.c"
IMPL="${SCRIPT_DIR}/dp_select_obliv_gumbel_argmax.c"

[[ -f "${HARNESS}" ]] || error "Missing harness file: ${HARNESS}"
[[ -f "${IMPL}" ]] || error "Missing implementation file: ${IMPL}"

echo "== Generating circuit with cbmc-gc =="
echo "Binary     : ${CBMC_GC_BIN}"
echo "Out dir    : ${OUTDIR}"
echo "Num records: ${NUM_RECORDS}"
echo "Num choices: ${NUM_CHOICES}"
echo "Frac bits  : ${FRAC_BITS}"
echo "Macro NPARTS: ${NPARTS}"
echo "Unwind bound: ${UNWIND}"
if [[ "${NO_MINIMIZATION}" == "true" ]]; then
  echo "Minimization: DISABLED (will skip SAT-based equivalence check)"
else
  echo "Minimization: ENABLED with ${MINIMIZATION_TIMEOUT_MINUTES} minute timeout"
fi
echo

# Build CBMC arguments
CBMC_ARGS=(
  --function mpc_main
  --outdir "${OUTDIR}"
  --unwind "${UNWIND}"
  --unwinding-assertions
  -DNUM_RECORDS="${NUM_RECORDS}"
  -DNUM_CHOICES="${NUM_CHOICES}"
  -DFRAC_BITS="${FRAC_BITS}"
  -DNPARTS="${NPARTS}"
)

# Add minimization options
if [[ "${NO_MINIMIZATION}" == "true" ]]; then
  CBMC_ARGS+=(--no-minimization)
else
  # Convert minutes to seconds for CBMC
  MINIMIZATION_TIMEOUT_SECONDS=$((MINIMIZATION_TIMEOUT_MINUTES * 60))
  CBMC_ARGS+=(--minimization-time-limit "${MINIMIZATION_TIMEOUT_SECONDS}")
fi

# Add any extra arguments
CBMC_ARGS+=("${EXTRA_CBMC_ARGS[@]}")

# Add input files
CBMC_ARGS+=("${IMPL}" "${HARNESS}")

set -x
"${CBMC_GC_BIN}" "${CBMC_ARGS[@]}"
set +x

echo
echo "Circuit artifacts written to ${OUTDIR}"
ls -1 "${OUTDIR}"
