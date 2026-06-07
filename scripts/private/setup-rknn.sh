#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

REPO_URL="https://github.com/airockchip/rknn-toolkit2.git"
BRANCH="${BRANCH:-master}"
WORK_DIR="${WORK_DIR:-/work/var/rknn-dev}"
DEPS_DIR="${DEPS_DIR:-/work/deps}"

usage() {
    cat <<'EOF'
Build and stage RKNN C/C++ runtime files for PEK.

Usage:
  scripts/private/setup-rknn.sh [work-dir]

Default:
  scripts/private/setup-rknn.sh /work/var/rknn-dev

Options:
  --work-dir DIR    Directory used for clone, temp, and sparse checkout state.
  --deps-dir DIR    Root dependency staging directory. Default: /work/deps.
  --help            Show this help.

Environment:
  WORK_DIR    Same as the positional work-dir argument.
  DEPS_DIR    Same as --deps-dir.
  BRANCH      rknn-toolkit2 branch to fetch. Default: master.
  RKNN_ARCH   Optional runtime architecture override. By default this is
              detected from the current container architecture.

Output:
  $DEPS_DIR/rknn/include
  $DEPS_DIR/rknn/lib
EOF
}

die() {
    echo "[ERROR] $*" >&2
    exit 1
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

detect_rknn_arch() {
    local machine
    machine="$(uname -m)"

    case "${machine}" in
        aarch64 | arm64)
            printf 'aarch64\n'
            ;;
        armhf | armv6l | armv7l | armv8l)
            printf 'armhf\n'
            ;;
        *)
            die "Unsupported container architecture: ${machine}. Supported RKNN runtimes: aarch64, armhf, armhf-uclibc"
            ;;
    esac
}

need_cmd() {
    if ! command -v "$1" > /dev/null 2>&1; then
        die "Required command not found: $1"
    fi
}

ORIGINAL_CWD="$(pwd -P)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --work-dir)
            [[ $# -ge 2 ]] || die "--work-dir requires a value"
            WORK_DIR="$2"
            shift 2
            ;;
        --deps-dir)
            [[ $# -ge 2 ]] || die "--deps-dir requires a value"
            DEPS_DIR="$2"
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
            WORK_DIR="$1"
            shift
            ;;
    esac
done

WORK_DIR="$(resolve_path "${WORK_DIR}")"
DEPS_DIR="$(resolve_path "${DEPS_DIR}")"
[[ "${WORK_DIR}" != "/" ]] || die "Refusing to use / as work directory"
[[ "${DEPS_DIR}" != "/" ]] || die "Refusing to use / as deps directory"

ARCH="${RKNN_ARCH:-$(detect_rknn_arch)}"
case "${ARCH}" in
    aarch64 | armhf | armhf-uclibc) ;;
    *)
        die "Unsupported RKNN_ARCH: ${ARCH}. Supported: aarch64, armhf, armhf-uclibc"
        ;;
esac

SRC_DIR="${WORK_DIR}/rknn-toolkit2"
STAGE_DIR="${WORK_DIR}/rknn-stage"
STAGE_INCLUDE="${STAGE_DIR}/include"
STAGE_LIB="${STAGE_DIR}/lib"
DST_DIR="${DEPS_DIR}/rknn"
DST_INCLUDE="${DST_DIR}/include"
DST_LIB="${DST_DIR}/lib"

echo "[INFO] Work dir: ${WORK_DIR}"
echo "[INFO] Deps dir: ${DEPS_DIR}"
echo "[INFO] Using RKNN runtime architecture: ${ARCH}"

need_cmd git
need_cmd cp
need_cmd rm
need_cmd mkdir

echo "[INFO] Cleaning RKNN-specific dirs only..."
rm -rf "${SRC_DIR}"
rm -rf "${STAGE_DIR}"

mkdir -p "${WORK_DIR}"
mkdir -p "${STAGE_INCLUDE}"
mkdir -p "${STAGE_LIB}"

echo "[INFO] Marking repo path as safe for git..."
git config --global --add safe.directory "${SRC_DIR}" || true

echo "[INFO] Initializing sparse repo in ${SRC_DIR}..."
git init "${SRC_DIR}"
cd "${SRC_DIR}"

git remote add origin "${REPO_URL}"
git config core.sparseCheckout true

mkdir -p .git/info
cat > .git/info/sparse-checkout << EOF
rknpu2/runtime/Linux/librknn_api/include/*
rknpu2/runtime/Linux/librknn_api/${ARCH}/*
EOF

echo "[INFO] Pulling only required files from ${BRANCH}..."
git pull --depth 1 origin "${BRANCH}"

RUNTIME_DIR="rknpu2/runtime/Linux/librknn_api"
INCLUDE_DIR="${RUNTIME_DIR}/include"
LIB_DIR="${RUNTIME_DIR}/${ARCH}"

if [[ ! -d "${INCLUDE_DIR}" ]]; then
    echo "[ERROR] Include dir not found: ${INCLUDE_DIR}"
    exit 1
fi

if [[ ! -d "${LIB_DIR}" ]]; then
    echo "[ERROR] Lib dir not found for ARCH=${ARCH}: ${LIB_DIR}"
    exit 1
fi

echo "[INFO] Copying headers..."
cp -r "${INCLUDE_DIR}/." "${STAGE_INCLUDE}/"

echo "[INFO] Copying libraries..."
cp -r "${LIB_DIR}/." "${STAGE_LIB}/"

echo "[INFO] Staging RKNN runtime into ${DST_DIR}..."
rm -rf "${DST_DIR}"
mkdir -p "$(dirname -- "${DST_DIR}")"
mv "${STAGE_DIR}" "${DST_DIR}"

echo
echo "[SUCCESS] RKNN C/C++ runtime files ready"
echo "  include -> ${DST_INCLUDE}"
echo "  lib     -> ${DST_LIB}"
