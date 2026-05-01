#!/usr/bin/env bash
set -euo pipefail

WORK_DIR="/work"
SRC_DIR="${WORK_DIR}/var/rknn-toolkit2"
DST_DIR="${WORK_DIR}/rknn"
DST_INCLUDE="${DST_DIR}/include"
DST_LIB="${DST_DIR}/lib"

REPO_URL="https://github.com/airockchip/rknn-toolkit2.git"
BRANCH="${BRANCH:-master}"
ARCH="${1:-aarch64}"

echo "[INFO] Using ARCH=${ARCH}"

case "${ARCH}" in
    aarch64|armhf|armhf-uclibc) ;;
    *)
        echo "[ERROR] Unsupported ARCH: ${ARCH}"
        echo "Supported: aarch64, armhf, armhf-uclibc"
        exit 1
        ;;
esac

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $1"
        exit 1
    fi
}

need_cmd git
need_cmd cp
need_cmd rm
need_cmd mkdir

echo "[INFO] Cleaning RKNN-specific dirs only..."
rm -rf "${SRC_DIR}"
rm -rf "${DST_DIR}"

mkdir -p "${WORK_DIR}/var"
mkdir -p "${DST_INCLUDE}"
mkdir -p "${DST_LIB}"

echo "[INFO] Marking repo path as safe for git..."
git config --global --add safe.directory "${SRC_DIR}" || true

echo "[INFO] Initializing sparse repo in ${SRC_DIR}..."
git init "${SRC_DIR}"
cd "${SRC_DIR}"

git remote add origin "${REPO_URL}"
git config core.sparseCheckout true

mkdir -p .git/info
cat > .git/info/sparse-checkout <<EOF
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
cp -r "${INCLUDE_DIR}/." "${DST_INCLUDE}/"

echo "[INFO] Copying libraries..."
cp -r "${LIB_DIR}/." "${DST_LIB}/"

echo
echo "[SUCCESS] RKNN C/C++ runtime files ready"
echo "  include -> ${DST_INCLUDE}"
echo "  lib     -> ${DST_LIB}"