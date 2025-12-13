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
  
  # Ensure PATH is in ~/.bashrc for persistent availability
  echo
  echo "== Ensuring CBMC is in ~/.bashrc =="
  PATH_EXPORT="export PATH=\"${PREFIX_DIR}/bin:\$PATH\""
  if ! grep -Fxq "${PATH_EXPORT}" ~/.bashrc 2>/dev/null; then
    echo "" >> ~/.bashrc
    echo "# CBMC installation" >> ~/.bashrc
    echo "${PATH_EXPORT}" >> ~/.bashrc
    echo "Added CBMC PATH to ~/.bashrc"
  else
    echo "CBMC PATH already in ~/.bashrc"
  fi
  
  echo
  echo "== Done. CBMC is ready to use. =="
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
  
  # Add to ~/.bashrc for persistent availability
  echo
  echo "== Adding CBMC to ~/.bashrc for persistent availability =="
  PATH_EXPORT="export PATH=\"${PREFIX_DIR}/bin:\$PATH\""
  if ! grep -Fxq "${PATH_EXPORT}" ~/.bashrc 2>/dev/null; then
    echo "" >> ~/.bashrc
    echo "# CBMC installation" >> ~/.bashrc
    echo "${PATH_EXPORT}" >> ~/.bashrc
    echo "Added CBMC PATH to ~/.bashrc"
  else
    echo "CBMC PATH already in ~/.bashrc"
  fi
  
  echo
  echo "== Done. CBMC is now available in all future shell sessions. =="
  echo "== For current shell, run: source ~/.bashrc =="
fi


