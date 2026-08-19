#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/Tencent/ncnn.git}"
BRANCH="${BRANCH:-master}"
WORK_DIR=""
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 1)}"
NCNN_VULKAN="${NCNN_VULKAN:-OFF}"
NCNN_BUILD_TOOLS="${NCNN_BUILD_TOOLS:-OFF}"
NCNN_SHARED_LIBS="${NCNN_SHARED_LIBS:-OFF}"

usage() {
    cat << 'EOF'
Build and stage NCNN C/C++ development files for PEK.

Usage:
  scripts/private/ncnn/setup-ncnn.sh <work-dir> [options]

Example:
  scripts/private/ncnn/setup-ncnn.sh var/ncnn-dev

Options:
  --work-dir DIR    Directory used for clone, build, temp, and staging state.
  --deps-dir DIR    Root dependency staging directory. Default: $PEK_PROJECT_ROOT/deps.
  --jobs N          Build parallelism. Default: CPU count.
  --help            Show this help.

Environment:
  PEK_PROJECT_ROOT  PEK checkout root. Default: checkout containing this script.
  DEPS_DIR          Same as --deps-dir.
  REPO_URL          NCNN repository URL. Default: https://github.com/Tencent/ncnn.git.
  BRANCH            NCNN branch/tag to fetch. Default: master.
  BUILD_TYPE        CMake build type. Default: Release.
  JOBS              Same as --jobs.
  NCNN_VULKAN       Build NCNN Vulkan support. Default: OFF.
  NCNN_BUILD_TOOLS  Build NCNN C++ tools with the SDK. Default: OFF.
  NCNN_SHARED_LIBS  Build NCNN shared library. Default: OFF.

Output:
  $DEPS_DIR/ncnn/include
  $DEPS_DIR/ncnn/lib
EOF
}

die() {
    echo "[ERROR] $*" >&2
    exit 1
}

resolve_project_root() {
    local requested_root="${PEK_PROJECT_ROOT:-$SCRIPT_DIR/../../..}"

    [[ "$requested_root" == /* ]] || die "PEK_PROJECT_ROOT must be an absolute path: $requested_root"
    [[ -d "$requested_root" ]] || die "PEK project root does not exist: $requested_root"

    local resolved_root
    resolved_root="$(cd -- "$requested_root" && pwd -P)"
    [[ -f "$resolved_root/development/meson.build" ]] ||
        die "PEK_PROJECT_ROOT is not a PEK checkout: $resolved_root"
    printf '%s\n' "$resolved_root"
}

resolve_path() {
    local path="$1"
    local parent
    local base

    case "${path}" in
        /*) ;;
        *) path="${ORIGINAL_CWD}/${path}" ;;
    esac

    parent="$(dirname -- "${path}")"
    base="$(basename -- "${path}")"
    mkdir -p "${parent}"
    parent="$(cd -- "${parent}" && pwd -P)"
    printf '%s/%s\n' "${parent}" "${base}"
}

need_cmd() {
    if ! command -v "$1" > /dev/null 2>&1; then
        die "Required command not found: $1"
    fi
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PEK_PROJECT_ROOT="$(resolve_project_root)"
export PEK_PROJECT_ROOT
DEPS_DIR="${DEPS_DIR:-$PEK_PROJECT_ROOT/deps}"
ORIGINAL_CWD="$(pwd -P)"
WORK_DIR_ARG_PROVIDED=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --work-dir)
            [[ $# -ge 2 ]] || die "--work-dir requires a value"
            [[ "${WORK_DIR_ARG_PROVIDED}" -eq 0 ]] || die "work-dir specified more than once"
            WORK_DIR="$2"
            WORK_DIR_ARG_PROVIDED=1
            shift 2
            ;;
        --deps-dir)
            [[ $# -ge 2 ]] || die "--deps-dir requires a value"
            DEPS_DIR="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"
            shift 2
            ;;
        --help | -h)
            usage
            exit 0
            ;;
        --*)
            die "Unknown option: $1"
            ;;
        *)
            [[ "${WORK_DIR_ARG_PROVIDED}" -eq 0 ]] || die "work-dir specified more than once"
            WORK_DIR="$1"
            WORK_DIR_ARG_PROVIDED=1
            shift
            ;;
    esac
done

if [[ "${WORK_DIR_ARG_PROVIDED}" -eq 0 || -z "${WORK_DIR}" ]]; then
    usage >&2
    die "work-dir argument is mandatory"
fi

WORK_DIR="$(resolve_path "${WORK_DIR}")"
DEPS_DIR="$(resolve_path "${DEPS_DIR}")"
[[ "${WORK_DIR}" != "/" ]] || die "Refusing to use / as work directory"
[[ "${DEPS_DIR}" != "/" ]] || die "Refusing to use / as deps directory"

SRC_DIR="${WORK_DIR}/ncnn"
BUILD_DIR="${SRC_DIR}/build"
STAGE_DIR="${WORK_DIR}/ncnn-stage"
DST_DIR="${DEPS_DIR}/ncnn"
DST_INCLUDE="${DST_DIR}/include"
DST_LIB="${DST_DIR}/lib"

echo "[INFO] Work dir: ${WORK_DIR}"
echo "[INFO] Deps dir: ${DEPS_DIR}"
echo "[INFO] NCNN ref: ${BRANCH}"
echo "[INFO] Build type: ${BUILD_TYPE}"
echo "[INFO] Container architecture: $(uname -m)"

if [[ -d "${DST_DIR}/bin" && ! -d "${DST_INCLUDE}" && ! -d "${DST_LIB}" ]]; then
    echo "[INFO] Existing ${DST_DIR} looks like old conversion-tool output; it will be replaced after a successful SDK build."
fi

need_cmd git
need_cmd cmake
need_cmd cp
need_cmd find
need_cmd grep
need_cmd mkdir
need_cmd mv
need_cmd rm

if command -v ninja > /dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=(-G Ninja)
else
    CMAKE_GENERATOR_ARGS=()
fi

echo "[INFO] Cleaning NCNN-specific dirs only..."
rm -rf "${SRC_DIR}"
rm -rf "${STAGE_DIR}"

mkdir -p "${WORK_DIR}"

echo "[INFO] Cloning NCNN source into ${SRC_DIR}..."
git clone --depth 1 --branch "${BRANCH}" "${REPO_URL}" "${SRC_DIR}"

echo "[INFO] Updating NCNN submodules..."
git -C "${SRC_DIR}" submodule update --init --recursive --depth 1

echo "[INFO] Configuring NCNN..."
cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" "${CMAKE_GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${STAGE_DIR}" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DNCNN_VULKAN="${NCNN_VULKAN}" \
    -DNCNN_BUILD_TESTS=OFF \
    -DNCNN_BUILD_BENCHMARK=OFF \
    -DNCNN_BUILD_EXAMPLES=OFF \
    -DNCNN_BUILD_TOOLS="${NCNN_BUILD_TOOLS}" \
    -DNCNN_SHARED_LIBS="${NCNN_SHARED_LIBS}"

echo "[INFO] Building NCNN with ${JOBS} job(s)..."
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

echo "[INFO] Installing NCNN into staging directory..."
cmake --install "${BUILD_DIR}" --prefix "${STAGE_DIR}"

if [[ ! -f "${STAGE_DIR}/include/ncnn/net.h" ]]; then
    die "NCNN header not found in staged include directory"
fi

if ! find "${STAGE_DIR}/lib" -type f -name 'libncnn*' -print -quit | grep -q .; then
    die "NCNN library not found in staged lib directory"
fi

echo "[INFO] Staging NCNN SDK into ${DST_DIR}..."
rm -rf "${DST_DIR}"
mkdir -p "$(dirname -- "${DST_DIR}")"
mv "${STAGE_DIR}" "${DST_DIR}"

echo
echo "[SUCCESS] NCNN C/C++ development files ready"
echo "  include -> ${DST_INCLUDE}"
echo "  lib     -> ${DST_LIB}"
