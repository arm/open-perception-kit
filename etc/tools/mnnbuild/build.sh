#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# Build MNN static library + MNNConvert converter.
# - Clones/updates source into this script's directory.
# - Builds into this script's directory.
# - Installs into: /work/deps/mnn

INSTALL_PREFIX="/work/deps/mnn"

# Optional overrides:
#   MNN_REF=master|<tag>|<commit>   (default: master)
#   JOBS=8                         (default: nproc)
#   FULLY_STATIC_EXE=1             (Linux only; may require musl/static toolchain)
MNN_REF="${MNN_REF:-master}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 4)}"
FULLY_STATIC_EXE="${FULLY_STATIC_EXE:-0}"

# Resolve script directory (works even if invoked via symlink)
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

SRC_DIR="${SCRIPT_DIR}/MNN"
BUILD_DIR="${SCRIPT_DIR}/build-mnn-static"

echo "[mnn] Script dir  : ${SCRIPT_DIR}"
echo "[mnn] Source dir  : ${SRC_DIR}"
echo "[mnn] Build dir   : ${BUILD_DIR}"
echo "[mnn] Install dir : ${INSTALL_PREFIX}"
echo "[mnn] Ref         : ${MNN_REF}"
echo "[mnn] Jobs        : ${JOBS}"

# --- Fetch / update source into script dir ---
if [[ ! -d "${SRC_DIR}/.git" ]]; then
    echo "[mnn] Cloning MNN..."
    git clone --depth 1 --branch "${MNN_REF}" https://github.com/alibaba/MNN.git "${SRC_DIR}"
else
    echo "[mnn] Updating existing MNN repo..."
    (   
        cd "${SRC_DIR}"
        git fetch --tags --prune
        git checkout "${MNN_REF}"
        # Best-effort fast-forward if on a branch
        git pull --ff-only || true
    )
fi

# --- Pick generator ---
GENERATOR_ARGS=()
if command -v ninja > /dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

# --- Optional fully-static executable flags (Linux only; may fail on glibc images) ---
EXE_LINK_FLAGS=()
if [[ "${FULLY_STATIC_EXE}" == "1" ]]; then
    if [[ "$(uname -s)" == "Linux" ]]; then
        EXE_LINK_FLAGS=(-DCMAKE_EXE_LINKER_FLAGS=-static)
        echo "[mnn] FULLY_STATIC_EXE=1: attempting fully static converter binary (-static)."
    else
        echo "[mnn] FULLY_STATIC_EXE=1 requested, but full static executables are not generally supported on $(uname -s)."
        echo "[mnn] Proceeding with static libMNN.a and normally linked converter."
    fi
fi

# --- Configure/build/install ---
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DMNN_BUILD_SHARED_LIBS=OFF \
    -DMNN_BUILD_CONVERTER=ON \
    -DMNN_BUILD_TEST=OFF \
    -DMNN_BUILD_BENCHMARK=OFF \
    -DMNN_BUILD_DEMO=OFF \
    "${EXE_LINK_FLAGS[@]}"

cmake --build "${BUILD_DIR}" -j "${JOBS}"
cmake --install "${BUILD_DIR}"

# --- Report outputs ---
echo
echo "[mnn] Install contents:"
ls -la "${INSTALL_PREFIX}/lib" 2> /dev/null || true
ls -la "${INSTALL_PREFIX}/bin" 2> /dev/null || true

if [[ -x "${INSTALL_PREFIX}/bin/MNNConvert" ]]; then
    echo
    echo "[mnn] Converter linkage check:"
    if command -v ldd > /dev/null 2>&1; then
        ldd "${INSTALL_PREFIX}/bin/MNNConvert" || true
    elif command -v otool > /dev/null 2>&1; then
        otool -L "${INSTALL_PREFIX}/bin/MNNConvert" || true
    else
        echo "  (No ldd/otool available)"
    fi
fi

echo
echo "[mnn] Done."
echo "  Static lib  : ${INSTALL_PREFIX}/lib/libMNN.a"
echo "  Converter   : ${INSTALL_PREFIX}/bin/MNNConvert"
