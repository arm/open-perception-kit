#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Build and stage ExecuTorch development files for PEK.

Usage:
  scripts/private/executorch/setup-executorch-deps.sh <work-dir> [options]

Example:
  scripts/private/executorch/setup-executorch-deps.sh /work/var/executorch-build

Options:
  --work-dir DIR          Directory used for clone, venv, downloads, build, temp, and caches.
  --deps-dir DIR          Root dependency staging directory. Default: /work/deps
  --executorch-ref REF    ExecuTorch branch/tag/commit. Default: release/1.0
  --executorch-repo URL   ExecuTorch repository URL.
  --jobs N                Build parallelism. Default: 1
  --keep-work-dir         Reuse the existing work directory instead of deleting it.
  --keep-build            Reuse the existing CMake build directory.
  --help                  Show this help.

Environment:
  DEPS_DIR                Same as --deps-dir.
  EXECUTORCH_REF          Same as --executorch-ref.
  EXECUTORCH_REPO         Same as --executorch-repo.
  EXECUTORCH_INSTALL_DIR  Default: $DEPS_DIR/executorch
  LIBTORCH_INSTALL_DIR    Default: $DEPS_DIR/libtorch
  VENV_DIR                Default: $WORK_DIR/.venv
  PYTHON_VERSION          Default: 3.11
  JOBS                    Same as --jobs.
  LIBTORCH_URL            Optional libtorch zip URL. If unset, torch headers are
                          copied from the ExecuTorch Python venv when available.

Output:
  $DEPS_DIR/executorch/include
  $DEPS_DIR/executorch/lib
  $DEPS_DIR/libtorch/include

All clone, build, venv, download, cache, and temporary state is kept under
the selected work directory. The only intentional output outside it is DEPS_DIR.
EOF
}

log() {
    printf '[executorch-deps] %s\n' "$*"
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" > /dev/null 2>&1 || die "missing required command: $1"
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

safe_rm_rf() {
    local path="$1"
    local allow_work_dir="${2:-0}"

    case "${path}" in
        "" | "/" | ".")
            die "refusing to remove unsafe path: ${path}"
            ;;
    esac

    if [[ "${path}" == "${PROJECT_ROOT}" || "${path}" == "${DEPS_DIR}" ||
          "${path}" == "${EXECUTORCH_DIR}" ]]; then
        die "refusing to remove protected path: ${path}"
    fi

    if [[ "${path}" == "${WORK_DIR}" && "${allow_work_dir}" != "1" ]]; then
        die "refusing to remove protected path: ${path}"
    fi

    rm -rf "${path}"
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
ORIGINAL_CWD="$(pwd -P)"

WORK_DIR=""
DEPS_DIR="${DEPS_DIR:-/work/deps}"
EXECUTORCH_REPO="${EXECUTORCH_REPO:-https://github.com/pytorch/executorch.git}"
EXECUTORCH_REF="${EXECUTORCH_REF:-release/1.0}"
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"
JOBS="${JOBS:-1}"
CLEAN_BUILD=1
CLEAN_WORK_DIR=1
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
        --executorch-ref)
            [[ $# -ge 2 ]] || die "--executorch-ref requires a value"
            EXECUTORCH_REF="$2"
            shift 2
            ;;
        --executorch-repo)
            [[ $# -ge 2 ]] || die "--executorch-repo requires a value"
            EXECUTORCH_REPO="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"
            shift 2
            ;;
        --keep-work-dir)
            CLEAN_WORK_DIR=0
            shift
            ;;
        --keep-build)
            CLEAN_BUILD=0
            shift
            ;;
        --help | -h)
            usage
            exit 0
            ;;
        --*)
            die "unknown option: $1"
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

EXECUTORCH_DIR="${EXECUTORCH_DIR:-${WORK_DIR}/executorch}"
BUILD_DIR="${BUILD_DIR:-${EXECUTORCH_DIR}/build}"
VENV_DIR="${VENV_DIR:-${WORK_DIR}/.venv}"
EXECUTORCH_INSTALL_DIR="${EXECUTORCH_INSTALL_DIR:-${DEPS_DIR}/executorch}"
LIBTORCH_INSTALL_DIR="${LIBTORCH_INSTALL_DIR:-${DEPS_DIR}/libtorch}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-${WORK_DIR}/downloads}"
LIBTORCH_URL="${LIBTORCH_URL:-}"

EXECUTORCH_DIR="$(resolve_path "${EXECUTORCH_DIR}")"
BUILD_DIR="$(resolve_path "${BUILD_DIR}")"
VENV_DIR="$(resolve_path "${VENV_DIR}")"
EXECUTORCH_INSTALL_DIR="$(resolve_path "${EXECUTORCH_INSTALL_DIR}")"
LIBTORCH_INSTALL_DIR="$(resolve_path "${LIBTORCH_INSTALL_DIR}")"
DOWNLOAD_DIR="$(resolve_path "${DOWNLOAD_DIR}")"

export TMPDIR="${WORK_DIR}/tmp"
export UV_CACHE_DIR="${WORK_DIR}/cache/uv"
export PIP_CACHE_DIR="${WORK_DIR}/cache/pip"
export XDG_CACHE_HOME="${WORK_DIR}/cache/xdg"
export MAX_JOBS="${JOBS}"
export CMAKE_BUILD_PARALLEL_LEVEL="${JOBS}"
export USE_KINETO="${USE_KINETO:-0}"

need_cmd git
need_cmd cmake

if command -v ninja > /dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=(-G Ninja)
else
    need_cmd make
    CMAKE_GENERATOR_ARGS=()
fi

create_venv() {
    log "Preparing Python ${PYTHON_VERSION} venv: ${VENV_DIR}"
    mkdir -p "${WORK_DIR}"

    if command -v uv > /dev/null 2>&1; then
        (cd "${WORK_DIR}" && uv venv --no-project --python "${PYTHON_VERSION}" --seed "${VENV_DIR}")
    else
        local pybin
        pybin="$(command -v "python${PYTHON_VERSION}" || true)"
        [[ -n "${pybin}" ]] || die "Python ${PYTHON_VERSION} not found and uv is unavailable"
        "${pybin}" -m venv "${VENV_DIR}"
    fi

    # shellcheck disable=SC1091
    source "${VENV_DIR}/bin/activate"
    python - << 'PY'
import sys
if sys.version_info[:2] != (3, 11):
    raise SystemExit(f"ExecuTorch build requires Python 3.11, got {sys.version.split()[0]}")
PY

    if python -m pip --version > /dev/null 2>&1; then
        (cd "${WORK_DIR}" && python -m pip install --upgrade pip)
    elif command -v uv > /dev/null 2>&1; then
        (cd "${WORK_DIR}" && uv pip install --python "${VENV_DIR}/bin/python" --upgrade pip)
    else
        die "pip is unavailable in ${VENV_DIR}; install Python venv support or use uv"
    fi
}

checkout_executorch() {
    log "Preparing ExecuTorch source: ${EXECUTORCH_DIR}"
    mkdir -p "$(dirname -- "${EXECUTORCH_DIR}")"

    if [[ ! -d "${EXECUTORCH_DIR}/.git" ]]; then
        if [[ -e "${EXECUTORCH_DIR}" ]]; then
            die "${EXECUTORCH_DIR} exists but is not a git checkout"
        fi
        git clone --branch "${EXECUTORCH_REF}" "${EXECUTORCH_REPO}" "${EXECUTORCH_DIR}"
    else
        git -C "${EXECUTORCH_DIR}" fetch --tags origin "${EXECUTORCH_REF}"
        git -C "${EXECUTORCH_DIR}" checkout "${EXECUTORCH_REF}"
    fi

    log "Updating ExecuTorch externals"
    git -C "${EXECUTORCH_DIR}" submodule sync --recursive
    git -C "${EXECUTORCH_DIR}" submodule update --init --recursive
}

install_executorch_python_deps() {
    log "Installing ExecuTorch Python/build dependencies"
    # shellcheck disable=SC1091
    source "${VENV_DIR}/bin/activate"
    (cd "${EXECUTORCH_DIR}" && ./install_executorch.sh)
}

configure_and_build_executorch() {
    log "Configuring ExecuTorch CMake build"
    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        safe_rm_rf "${BUILD_DIR}"
    fi

    cmake -S "${EXECUTORCH_DIR}" -B "${BUILD_DIR}" "${CMAKE_GENERATOR_ARGS[@]}" \
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
        -DCMAKE_INSTALL_PREFIX="${EXECUTORCH_INSTALL_DIR}"

    log "Building ExecuTorch with ${JOBS} job(s)"
    cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
}

copy_built_libraries() {
    local libdir="${EXECUTORCH_INSTALL_DIR}/lib"
    safe_rm_rf "${EXECUTORCH_INSTALL_DIR}"
    mkdir -p "${libdir}"

    log "Installing ExecuTorch to ${EXECUTORCH_INSTALL_DIR}"
    cmake --install "${BUILD_DIR}" --prefix "${EXECUTORCH_INSTALL_DIR}"

    log "Copying built static/shared libraries into ${libdir}"
    while IFS= read -r artifact; do
        cp -f "${artifact}" "${libdir}/"
    done < <(find "${BUILD_DIR}" -type f \( -name '*.a' -o -name '*.so' -o -name '*.so.*' \))
}

copy_libtorch_from_url() {
    [[ -n "${LIBTORCH_URL}" ]] || return 1
    need_cmd curl
    need_cmd unzip

    local tmp_dir="${DOWNLOAD_DIR}/libtorch"
    safe_rm_rf "${tmp_dir}"
    mkdir -p "${tmp_dir}"

    log "Downloading libtorch from ${LIBTORCH_URL}"
    curl --retry 5 --retry-all-errors -fL "${LIBTORCH_URL}" -o "${tmp_dir}/libtorch.zip"
    unzip -q "${tmp_dir}/libtorch.zip" -d "${tmp_dir}"

    local include_dir
    include_dir="$(find "${tmp_dir}" -type d -name include | head -n 1 || true)"
    [[ -n "${include_dir}" ]] || die "libtorch archive did not contain an include directory"

    local package_root
    package_root="$(dirname -- "${include_dir}")"

    safe_rm_rf "${LIBTORCH_INSTALL_DIR}"
    mkdir -p "${LIBTORCH_INSTALL_DIR}/include" "${LIBTORCH_INSTALL_DIR}/lib"
    cp -R "${package_root}/include/." "${LIBTORCH_INSTALL_DIR}/include/"

    if [[ -d "${package_root}/lib" ]]; then
        cp -R "${package_root}/lib/." "${LIBTORCH_INSTALL_DIR}/lib/"
    fi
}

copy_libtorch_from_venv() {
    # shellcheck disable=SC1091
    source "${VENV_DIR}/bin/activate"

    local torch_root
    torch_root="$(
                  python - << 'PY'
import importlib.util
from pathlib import Path

spec = importlib.util.find_spec("torch")
if spec and spec.origin:
    root = Path(spec.origin).parent
    if (root / "include").is_dir():
        print(root)
PY
    )"

    [[ -n "${torch_root}" ]] || return 1

    log "Copying libtorch headers from Python package: ${torch_root}"
    safe_rm_rf "${LIBTORCH_INSTALL_DIR}"
    mkdir -p "${LIBTORCH_INSTALL_DIR}/include" "${LIBTORCH_INSTALL_DIR}/lib"
    cp -R "${torch_root}/include/." "${LIBTORCH_INSTALL_DIR}/include/"

    if [[ -d "${torch_root}/lib" ]]; then
        cp -R "${torch_root}/lib/." "${LIBTORCH_INSTALL_DIR}/lib/"
    fi
}

stage_libtorch_headers() {
    if copy_libtorch_from_venv; then
        return 0
    fi

    if copy_libtorch_from_url; then
        return 0
    fi

    log "No torch headers found in the venv and LIBTORCH_URL is unset"
    log "Creating ${LIBTORCH_INSTALL_DIR}/include so Meson include paths remain valid"
    mkdir -p "${LIBTORCH_INSTALL_DIR}/include" "${LIBTORCH_INSTALL_DIR}/lib"
}

validate_staged_files() {
    log "Validating staged files for PEK"

    local required_headers=(
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/extension/module/module.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/extension/tensor/tensor_ptr_maker.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/runtime/core/error.h"
        "${LIBTORCH_INSTALL_DIR}/include"
    )

    local required_libs=(
        libextension_module.a
        libextension_tensor.a
        libextension_flat_tensor.a
        libextension_data_loader.a
        libextension_named_data_map.a
        libextension_threadpool.a
        libexecutorch.a
        libexecutorch_core.a
        libpthreadpool.a
        libcpuinfo.a
        libportable_ops_lib.a
        libportable_kernels.a
        libkleidiai.a
        libXNNPACK.a
        libxnnpack_backend.a
        libxnnpack-microkernels-prod.a
    )

    local missing=0
    local path
    for path in "${required_headers[@]}"; do
        if [[ ! -e "${path}" ]]; then
            printf 'Missing required header/path: %s\n' "${path}" >&2
            missing=1
        fi
    done

    local lib
    for lib in "${required_libs[@]}"; do
        path="${EXECUTORCH_INSTALL_DIR}/lib/${lib}"
        if [[ ! -f "${path}" ]]; then
            printf 'Missing required library: %s\n' "${path}" >&2
            missing=1
        fi
    done

    [[ "${missing}" -eq 0 ]] || die "ExecuTorch staging is incomplete"
}

print_summary() {
    cat << EOF

ExecuTorch development files are ready.

ExecuTorch:
  ${EXECUTORCH_INSTALL_DIR}

libtorch compatibility headers:
  ${LIBTORCH_INSTALL_DIR}

Build PEK with:
  PEK_EXECUTORCH=enabled ./scripts/build-elements.sh debug
EOF
}

log "Project root: ${PROJECT_ROOT}"
log "Work dir: ${WORK_DIR}"
log "Deps dir: ${DEPS_DIR}"
log "ExecuTorch ref: ${EXECUTORCH_REF}"

if [[ "${CLEAN_WORK_DIR}" -eq 1 ]]; then
    log "Deleting work dir before starting: ${WORK_DIR}"
    safe_rm_rf "${WORK_DIR}" 1
fi

mkdir -p "${WORK_DIR}"
mkdir -p "${TMPDIR}" "${UV_CACHE_DIR}" "${PIP_CACHE_DIR}" "${XDG_CACHE_HOME}"
cd "${WORK_DIR}"

create_venv
checkout_executorch
install_executorch_python_deps
configure_and_build_executorch
copy_built_libraries
stage_libtorch_headers
validate_staged_files
print_summary
