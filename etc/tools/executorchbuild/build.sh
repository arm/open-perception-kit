#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

PY311_PREFIX="${PY311_PREFIX:-/opt/python311}"
VENV_DIR="${VENV_DIR:-/opt/venv}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
EXECUTORCH_DIR="${EXECUTORCH_DIR:-${SCRIPT_DIR}/executorch}"
EXECUTORCH_BRANCH="${EXECUTORCH_BRANCH:-release/1.0}"

export MAX_JOBS=1
export CMAKE_BUILD_PARALLEL_LEVEL=1
export USE_KINETO=0

have_py311() {
    [[ -x "${PY311_PREFIX}/bin/python3.11" ]] && "${PY311_PREFIX}/bin/python3.11" -c 'import sys; assert sys.version_info[:2]==(3,11)'
}

install_py311_standalone() {
    echo "==> Trying to install Python 3.11 (standalone) into ${PY311_PREFIX}"

    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y ca-certificates curl xz-utils

    mkdir -p "${PY311_PREFIX}"

    arch="$(uname -m)"
    case "$arch" in
        x86_64) pyarch="x86_64-unknown-linux-gnu" ;;
        aarch64) pyarch="aarch64-unknown-linux-gnu" ;;
        *)
            echo "Unsupported arch for standalone download: ${arch}"
            return 1
            ;;
    esac

    # indygreg/python-build-standalone releases (GitHub)
    # We intentionally pick the "install_only" tarball variant.
    # If this URL 404s in the future, choose a newer release tag and matching filename.
    ver="3.11.7"
    rel="20240107"
    url="https://github.com/indygreg/python-build-standalone/releases/download/${rel}/cpython-${ver}+${rel}-${pyarch}-install_only.tar.gz"

    echo "==> Downloading: ${url}"
    tmp="/tmp/python311.tgz"
    curl -L --fail -o "$tmp" "$url"

    tar -xzf "$tmp" -C "${PY311_PREFIX}"
    rm -f "$tmp"

    # The archive typically contains bin/python3.11 already in prefix; sanity check:
    "${PY311_PREFIX}/bin/python3.11" -V
}

build_py311_from_source() {
    echo "==> Building Python 3.11 from source into ${PY311_PREFIX} (slow path)"

    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential wget \
        libssl-dev zlib1g-dev libbz2-dev libreadline-dev libsqlite3-dev \
        libncursesw5-dev xz-utils tk-dev libffi-dev liblzma-dev uuid-dev \
        ca-certificates

    ver="3.11.11"
    cd /tmp
    wget -O "Python-${ver}.tgz" "https://www.python.org/ftp/python/${ver}/Python-${ver}.tgz"
    tar -xzf "Python-${ver}.tgz"
    cd "Python-${ver}"

    ./configure --prefix="${PY311_PREFIX}" --enable-optimizations --with-ensurepip=install
    make -j"$(nproc)"
    make install

    "${PY311_PREFIX}/bin/python3.11" -V
}

ensure_py311() {
    if have_py311; then
        echo "==> Python 3.11 already present in ${PY311_PREFIX}"
        return 0
    fi

    # Try standalone first, fallback to source build
    if ! install_py311_standalone; then
        build_py311_from_source
    fi

    have_py311
}

ensure_venv() {
    echo "==> Creating venv at ${VENV_DIR} with Python 3.11"
    rm -rf "${VENV_DIR}"
    "${PY311_PREFIX}/bin/python3.11" -m venv "${VENV_DIR}"
    # shellcheck disable=SC1091
    source "${VENV_DIR}/bin/activate"
    python -m pip install --upgrade pip
}

build_executorch() {
    # shellcheck disable=SC1091
    source "${VENV_DIR}/bin/activate"
    python -V

    mkdir -p "$(dirname "${EXECUTORCH_DIR}")"
    cd "$(dirname "${EXECUTORCH_DIR}")"

    if [[ ! -d "${EXECUTORCH_DIR}/.git" ]]; then
        git clone -b "${EXECUTORCH_BRANCH}" https://github.com/pytorch/executorch.git "${EXECUTORCH_DIR}"
    fi

    cd "${EXECUTORCH_DIR}"

    ./install_executorch.sh

    rm -rf build
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DEXECUTORCH_BUILD_PYTHON=OFF \
        -DEXECUTORCH_BUILD_TESTS=OFF \
        -DEXECUTORCH_BUILD_EXAMPLES=OFF \
        -DEXECUTORCH_BUILD_EXTENSION_MODULE=ON \
        -DEXECUTORCH_BUILD_EXTENSION_FLAT_TENSOR=ON \
        -DEXECUTORCH_BUILD_EXTENSION_TENSOR=ON \
        -DEXECUTORCH_BUILD_EXTENSION_DATA_LOADER=ON \
        -DEXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP=ON \
        -DEXECUTORCH_BUILD_PORTABLE_OPS=ON \
        -DEXECUTORCH_BUILD_EXECUTOR_RUNNER=ON \
        -DEXECUTORCH_BUILD_XNNPACK=ON \
        -DEXECUTORCH_BUILD_XNNPACK_BACKEND=ON \
        -DEXECUTORCH_SELECT_ALL_OPS=ON \
        -DCMAKE_INSTALL_PREFIX=/opt/executorch

    cmake --build build --parallel 1
    cmake --install build --prefix /work/deps/executorch

}

# --- main ---
ensure_py311
ensure_venv
build_executorch
echo "==> Done."
