#!/bin/bash
set -euo pipefail

module purge
module load gcc/14.2.0
module load cmake/3.30.5
module load anaconda/2025.06-py313

# Create directories if they don't exist
mkdir -p /tmp/are_env
mkdir -p ~/Desktop/wei402_scratch/are_env/CBMC

# 1) Locations
ROOT="${HOME}/Desktop/wei402_scratch/are_env/CBMC"
SRC_DIR="${ROOT}/src"
BUILD_DIR="/tmp/are_env/CBMC/build"
PREFIX_DIR="/tmp/are_env/CBMC/prefix"

CBMC_REPO_DIR="${SRC_DIR}/cbmc"
CBMC_BUILD_DIR="${BUILD_DIR}/cbmc"

mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${PREFIX_DIR}"

echo "== Installing CBMC =="
echo "ROOT       : ${ROOT}"
echo "SRC        : ${CBMC_REPO_DIR}"
echo "BUILD      : ${CBMC_BUILD_DIR}"
echo "INSTALL(PREFIX): ${PREFIX_DIR}"
echo

# 2) Basic environment diagnostics
echo "== Toolchain =="
which gcc || true
gcc --version | head -n 2 || true
which g++ || true
g++ --version | head -n 2 || true
which cmake
cmake --version | head -n 2
echo

# 3) Clone or update CBMC
if [ -d "${CBMC_REPO_DIR}/.git" ]; then
  echo "== Use existing CBMC repo =="
else
  echo "== Cloning CBMC repo (shallow) =="
  git clone --depth 1 https://github.com/diffblue/cbmc.git "${CBMC_REPO_DIR}"
fi
echo

# 4) Check if CBMC is already installed
export PATH="${PREFIX_DIR}/bin:${PATH}"
if command -v cbmc >/dev/null 2>&1 && cbmc --version >/dev/null 2>&1; then
  echo "== CBMC already installed =="
  echo "== Verifying installation =="
  which cbmc
  cbmc --version | head -n 10
  
  echo
  echo "== Done. CBMC is ready to use. =="
  echo "== To use CBMC, run: export PATH=\"${PREFIX_DIR}/bin:\$PATH\" =="
else
  # Determine parallelism
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS="$(getconf _NPROCESSORS_ONLN || echo 4)"
  fi

  # Check if already built
  if [ -f "${CBMC_BUILD_DIR}/CMakeCache.txt" ]; then
    echo "== CBMC already configured and built =="
    echo "== Installing into ${PREFIX_DIR} =="
    cmake --install "${CBMC_BUILD_DIR}"
    
    echo
    echo "== Verifying installation =="
    export PATH="${PREFIX_DIR}/bin:${PATH}"
    which cbmc
    cbmc --version | head -n 10
  else
    echo "== Building CBMC from scratch =="
    echo "== Configuring (CMake) =="
    cmake -S "${CBMC_REPO_DIR}" -B "${CBMC_BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${PREFIX_DIR}" \
      -DWITH_JBMC=OFF

    echo
    echo "== Building (JOBS=${JOBS}) =="
    cmake --build "${CBMC_BUILD_DIR}" -j "${JOBS}"

    echo
    echo "== Installing into ${PREFIX_DIR} =="
    cmake --install "${CBMC_BUILD_DIR}"

    echo
    echo "== Verifying installation =="
    export PATH="${PREFIX_DIR}/bin:${PATH}"
    which cbmc
    cbmc --version | head -n 10
  fi
  
  echo
  echo "== Done. CBMC installation complete. =="
  echo "== To use CBMC, run: export PATH=\"${PREFIX_DIR}/bin:\$PATH\" =="
fi

echo
echo "================================================================================"
echo "== Installing CBMC-GC-2 =="
echo "================================================================================"

# CBMC-GC-2 Locations
CBMC_GC2_REPO_DIR="${SRC_DIR}/cbmc-gc-2"
CBMC_GC2_BUILD_DIR="${BUILD_DIR}/cbmc-gc-2"
CBMC_GC2_PREFIX_DIR="/tmp/are_env/CBMC-GC-2/prefix"

mkdir -p "${CBMC_GC2_PREFIX_DIR}"

echo "SRC        : ${CBMC_GC2_REPO_DIR}"
echo "BUILD      : ${CBMC_GC2_BUILD_DIR}"
echo "INSTALL(PREFIX): ${CBMC_GC2_PREFIX_DIR}"
echo

# Clone or update CBMC-GC-2
echo "== [CBMC-GC-2] Source checkout =="
if [ -d "${CBMC_GC2_REPO_DIR}/.git" ]; then
  echo "[GC2] Using existing repo: ${CBMC_GC2_REPO_DIR}"
  # Optional refresh
  git -C "${CBMC_GC2_REPO_DIR}" fetch --depth 1 origin || true
else
  echo "[GC2] Cloning into: ${CBMC_GC2_REPO_DIR}"
  git clone https://gitlab.com/securityengineering/CBMC-GC-2.git "${CBMC_GC2_REPO_DIR}"
fi
echo

# Check if CBMC-GC-2 is already installed
export PATH="${CBMC_GC2_REPO_DIR}:${PATH}"
if command -v cbmc-gc >/dev/null 2>&1; then
  echo "== CBMC-GC-2 already installed =="
  echo "== Verifying installation =="
  which cbmc-gc || true
  cbmc-gc --version 2>/dev/null | head -n 10 || true
  
  echo
  echo "== Done. CBMC-GC-2 is ready to use. =="
  echo "== To use CBMC-GC-2, run: export PATH=\"${CBMC_GC2_REPO_DIR}:\$PATH\" =="
else
  # Determine parallelism
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS="$(getconf _NPROCESSORS_ONLN || echo 4)"
  fi

  # Check if already built (look for the binary in the source directory)
  if [ -f "${CBMC_GC2_REPO_DIR}/cbmc-gc" ] || [ -f "${CBMC_GC2_REPO_DIR}/cbmc-gc.exe" ]; then
    echo "== CBMC-GC-2 already built =="
    echo "== Verifying installation =="
    export PATH="${CBMC_GC2_REPO_DIR}:${PATH}"
    which cbmc-gc || true
    cbmc-gc --version 2>/dev/null | head -n 10 || true
  else
    echo "== Building CBMC-GC-2 from scratch =="
    
    # Build in-place (CBMC-GC-2 usually builds in repo with make).
    # We'll log outputs into build dir to keep repo clean.
    GC2_LOG_DIR="${CBMC_GC2_BUILD_DIR}/logs"
    mkdir -p "${GC2_LOG_DIR}"

    pushd "${CBMC_GC2_REPO_DIR}" >/dev/null

    echo "== [GC2] minisat2 download/build =="
    make minisat2-download 2>&1 | tee "${GC2_LOG_DIR}/minisat2-download.log"

    echo
    echo "== [GC2] build =="
    make -j "${JOBS}" 2>&1 | tee "${GC2_LOG_DIR}/build.log"

    popd >/dev/null

    echo
    echo "== Verifying installation =="
    export PATH="${CBMC_GC2_REPO_DIR}:${PATH}"
    which cbmc-gc || true
    cbmc-gc --version 2>/dev/null | head -n 10 || true
  fi
  
  echo
  echo "== Done. CBMC-GC-2 installation complete. =="
  echo "== To use CBMC-GC-2, run: export PATH=\"${CBMC_GC2_REPO_DIR}:\$PATH\" =="
fi


