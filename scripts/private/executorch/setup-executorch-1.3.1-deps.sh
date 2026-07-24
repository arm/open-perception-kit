#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Build and stage ExecuTorch 1.3.1 development files for PEK.

Usage:
  scripts/private/executorch/setup-executorch-1.3.1-deps.sh <work-dir> [options]

Example:
  scripts/private/executorch/setup-executorch-1.3.1-deps.sh /work/var/executorch-1.3.1-build
  scripts/private/executorch/setup-executorch-1.3.1-deps.sh /work/var/executorch-1.3.1-arm-build --target-arch arm

Options:
  --work-dir DIR          Directory used for source, venv, downloads, build, temp, and caches.
  --deps-dir DIR          Root dependency staging directory. Default: /work/deps
  --target-arch ARCH      Build target: x86_64 (default) or arm (AArch64 Linux GNU).
  --executorch-url URL    ExecuTorch source archive URL. Default: official v1.3.1 tarball.
  --executorch-git-url URL
                          Git URL used only to recover pinned submodule commits.
  --jobs N                Build parallelism. Default: 1
  --deb-output-dir DIR    Debian package output directory. Default: /work/var
  --deb-revision REV      Debian package revision. Default: 2
  --skip-deb              Stage files without creating a Debian package.
  --keep-work-dir         Reuse the existing work directory instead of deleting it.
  --keep-build            Reuse the existing CMake build directory.
  --help                  Show this help.

Environment:
  DEPS_DIR                Same as --deps-dir.
  EXECUTORCH_TARGET_ARCH  Same as --target-arch.
  EXECUTORCH_ARCHIVE_URL  Same as --executorch-url.
  EXECUTORCH_GIT_URL      Same as --executorch-git-url.
  EXECUTORCH_INSTALL_DIR  Default: $DEPS_DIR/executorch
  VENV_DIR                Default: $WORK_DIR/.venv
  PYTHON_VERSION          Default: 3.11
  JOBS                    Same as --jobs.
  EXECUTORCH_X86_64_CC     x86_64 C compiler. Default: x86_64-linux-gnu-gcc-14
  EXECUTORCH_X86_64_CXX    x86_64 C++ compiler. Default: x86_64-linux-gnu-g++-14
  EXECUTORCH_X86_64_AR     x86_64 archiver. Default: x86_64-linux-gnu-ar
  EXECUTORCH_X86_64_RANLIB x86_64 ranlib. Default: x86_64-linux-gnu-ranlib
  EXECUTORCH_X86_64_STRIP  x86_64 strip tool. Default: x86_64-linux-gnu-strip
  EXECUTORCH_ARM_CC       Arm64 C compiler. Default: aarch64-linux-gnu-gcc-14
  EXECUTORCH_ARM_CXX      Arm64 C++ compiler. Default: aarch64-linux-gnu-g++-14
  EXECUTORCH_ARM_AR       Arm64 archiver. Default: aarch64-linux-gnu-ar
  EXECUTORCH_ARM_RANLIB   Arm64 ranlib. Default: aarch64-linux-gnu-ranlib
  EXECUTORCH_ARM_STRIP    Arm64 strip tool. Default: aarch64-linux-gnu-strip
  EXECUTORCH_DEB_OUTPUT_DIR
                          Same as --deb-output-dir.
  EXECUTORCH_DEB_REVISION Same as --deb-revision.
  EXECUTORCH_DEB_MAINTAINER
                          Debian Maintainer field. Default: Arm Limited

Output:
  $DEPS_DIR/executorch/include
  $DEPS_DIR/executorch/lib
  /work/var/libexecutorch-dev-1.3.1-<revision>-<architecture>.deb

The top-level ExecuTorch source is downloaded from the fixed archive. Git is
used only after extraction to recover pinned submodule commits from the v1.3.1
tag, because GitHub source archives do not include submodule contents.

All source, build, venv, download, cache, and temporary state is kept under the
selected work directory. The intentional outputs outside it are DEPS_DIR and
the Debian package output directory.
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

archive_architecture() {
    local archive="$1"
    local member
    local description

    IFS= read -r member < <(ar t "${archive}")
    [[ -n "${member}" ]] || die "cannot inspect empty or invalid archive: ${archive}"

    description="$(ar p "${archive}" "${member}" | file -b -)" ||
        die "cannot determine target architecture from ${archive}"
    case "${description}" in
        *x86-64* | *x86_64*)
            printf 'x86_64\n'
            ;;
        *aarch64* | *AArch64* | *arm64* | *ARM64*)
            printf 'arm64\n'
            ;;
        *arm* | *Arm* | *ARM*)
            printf 'arm\n'
            ;;
        *)
            die "unsupported target architecture in ${archive}: ${description}"
            ;;
    esac
}

normalize_target_architecture() {
    case "$1" in
        x86_64 | amd64)
            printf 'x86_64\n'
            ;;
        arm | arm64 | aarch64)
            printf 'arm64\n'
            ;;
        *)
            die "unsupported target architecture: $1 (expected x86_64 or arm/arm64/aarch64)"
            ;;
    esac
}

resolve_path() {
    local path="$1"

    case "${path}" in
        /*) ;;
        *) path="${ORIGINAL_CWD}/${path}" ;;
    esac

    realpath -m -- "${path}"
}

require_descendant_path() {
    local path="$1"
    local root="$2"
    local label="$3"
    local root_label="$4"

    [[ "${path}" == "${root}/"* ]] ||
        die "${label} must be inside ${root_label}: ${path}"
}

require_disjoint_paths() {
    local first="$1"
    local second="$2"
    local first_label="$3"
    local second_label="$4"

    if [[ "${first}" == "${second}" || "${first}" == "${second}/"* ||
          "${second}" == "${first}/"* ]]; then
        die "${first_label} and ${second_label} must not overlap"
    fi
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

    if [[ "${path}" == "${WORK_DIR}" ]]; then
        [[ "${allow_work_dir}" == "1" ]] ||
            die "refusing to remove protected path: ${path}"
    elif [[ "${path}" == "${WORK_DIR}/"* ]]; then
        :
    elif [[ "${path}" == "${EXECUTORCH_INSTALL_DIR}" ]]; then
        :
    else
        die "refusing to remove path outside the declared cleanup roots: ${path}"
    fi

    rm -rf "${path}"
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
ORIGINAL_CWD="$(pwd -P)"

WORK_DIR=""
DEPS_DIR="${DEPS_DIR:-/work/deps}"
EXECUTORCH_VERSION="1.3.1"
EXECUTORCH_ARCHIVE_URL="${EXECUTORCH_ARCHIVE_URL:-https://github.com/pytorch/executorch/archive/refs/tags/v1.3.1.tar.gz}"
EXECUTORCH_GIT_URL="${EXECUTORCH_GIT_URL:-https://github.com/pytorch/executorch.git}"
EXECUTORCH_ARCHIVE_SHA256="d315585bcc75efba0fb01f27794ecdf2b3e091350c4ac1340fd754ac722301dc"
EXECUTORCH_TAG_COMMIT="e2f18eb23c45bd22ca332b0b8b49a81de304b472"
PYTHON_BUILD_REQUIREMENTS="${SCRIPT_DIR}/python-build-requirements-1.3.1.txt"
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"
JOBS="${JOBS:-1}"
TARGET_ARCH="${EXECUTORCH_TARGET_ARCH:-x86_64}"
X86_64_C_COMPILER="${EXECUTORCH_X86_64_CC:-x86_64-linux-gnu-gcc-14}"
X86_64_CXX_COMPILER="${EXECUTORCH_X86_64_CXX:-x86_64-linux-gnu-g++-14}"
X86_64_AR="${EXECUTORCH_X86_64_AR:-x86_64-linux-gnu-ar}"
X86_64_RANLIB="${EXECUTORCH_X86_64_RANLIB:-x86_64-linux-gnu-ranlib}"
X86_64_STRIP="${EXECUTORCH_X86_64_STRIP:-x86_64-linux-gnu-strip}"
ARM_C_COMPILER="${EXECUTORCH_ARM_CC:-aarch64-linux-gnu-gcc-14}"
ARM_CXX_COMPILER="${EXECUTORCH_ARM_CXX:-aarch64-linux-gnu-g++-14}"
ARM_AR="${EXECUTORCH_ARM_AR:-aarch64-linux-gnu-ar}"
ARM_RANLIB="${EXECUTORCH_ARM_RANLIB:-aarch64-linux-gnu-ranlib}"
ARM_STRIP="${EXECUTORCH_ARM_STRIP:-aarch64-linux-gnu-strip}"
DEB_OUTPUT_DIR="${EXECUTORCH_DEB_OUTPUT_DIR:-/work/var}"
DEB_REVISION="${EXECUTORCH_DEB_REVISION:-2}"
BUILD_DEB=1
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
        --target-arch)
            [[ $# -ge 2 ]] || die "--target-arch requires a value"
            TARGET_ARCH="$2"
            shift 2
            ;;
        --executorch-url)
            [[ $# -ge 2 ]] || die "--executorch-url requires a value"
            EXECUTORCH_ARCHIVE_URL="$2"
            shift 2
            ;;
        --executorch-git-url)
            [[ $# -ge 2 ]] || die "--executorch-git-url requires a value"
            EXECUTORCH_GIT_URL="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"
            shift 2
            ;;
        --deb-output-dir)
            [[ $# -ge 2 ]] || die "--deb-output-dir requires a value"
            DEB_OUTPUT_DIR="$2"
            shift 2
            ;;
        --deb-revision)
            [[ $# -ge 2 ]] || die "--deb-revision requires a value"
            DEB_REVISION="$2"
            shift 2
            ;;
        --skip-deb)
            BUILD_DEB=0
            shift
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

TARGET_ARCH="$(normalize_target_architecture "${TARGET_ARCH}")"
EXECUTORCH_REQUIRED_LIBS=(
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
)
if [[ "${TARGET_ARCH}" == "arm64" ]]; then
    EXECUTORCH_REQUIRED_LIBS+=(libkleidiai.a)
fi
EXECUTORCH_REQUIRED_LIBS+=(
    libXNNPACK.a
    libxnnpack_backend.a
    libxnnpack-microkernels-prod.a
)
EXECUTORCH_BUILD_TARGETS=(
    flatccrt
    extension_module
    extension_module_static
    extension_tensor
    extension_flat_tensor
    extension_data_loader
    extension_named_data_map
    extension_threadpool
    executorch
    executorch_core
    pthreadpool
    cpuinfo
    portable_ops_lib
    portable_kernels
)
if [[ "${TARGET_ARCH}" == "arm64" ]]; then
    EXECUTORCH_BUILD_TARGETS+=(kleidiai)
fi
EXECUTORCH_BUILD_TARGETS+=(
    XNNPACK
    xnnpack_backend
    xnnpack-microkernels-prod
)
if [[ "${BUILD_DEB}" -eq 1 ]]; then
    [[ "${DEB_REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] ||
        die "invalid Debian package revision: ${DEB_REVISION}"
fi

need_cmd realpath
WORK_DIR="$(resolve_path "${WORK_DIR}")"
DEPS_DIR="$(resolve_path "${DEPS_DIR}")"
require_descendant_path "${WORK_DIR}" "${PROJECT_ROOT}" WORK_DIR PROJECT_ROOT
require_descendant_path "${DEPS_DIR}" "${PROJECT_ROOT}" DEPS_DIR PROJECT_ROOT

EXECUTORCH_DIR="${EXECUTORCH_DIR:-${WORK_DIR}/executorch}"
BUILD_DIR="${BUILD_DIR:-${EXECUTORCH_DIR}/build}"
VENV_DIR="${VENV_DIR:-${WORK_DIR}/.venv}"
EXECUTORCH_INSTALL_DIR="${EXECUTORCH_INSTALL_DIR:-${DEPS_DIR}/executorch}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-${WORK_DIR}/downloads}"

EXECUTORCH_DIR="$(resolve_path "${EXECUTORCH_DIR}")"
BUILD_DIR="$(resolve_path "${BUILD_DIR}")"
VENV_DIR="$(resolve_path "${VENV_DIR}")"
EXECUTORCH_INSTALL_DIR="$(resolve_path "${EXECUTORCH_INSTALL_DIR}")"
DOWNLOAD_DIR="$(resolve_path "${DOWNLOAD_DIR}")"
DEB_OUTPUT_DIR="$(resolve_path "${DEB_OUTPUT_DIR}")"

if [[ "${PROJECT_ROOT}" == "${WORK_DIR}" ||
      "${PROJECT_ROOT}" == "${WORK_DIR}/"* ]]; then
    die "WORK_DIR must not be the project root or one of its ancestors"
fi
require_disjoint_paths "${WORK_DIR}" "${DEPS_DIR}" WORK_DIR DEPS_DIR
require_descendant_path "${EXECUTORCH_DIR}" "${WORK_DIR}" EXECUTORCH_DIR WORK_DIR
require_descendant_path "${BUILD_DIR}" "${WORK_DIR}" BUILD_DIR WORK_DIR
require_descendant_path "${VENV_DIR}" "${WORK_DIR}" VENV_DIR WORK_DIR
require_descendant_path "${DOWNLOAD_DIR}" "${WORK_DIR}" DOWNLOAD_DIR WORK_DIR
require_descendant_path \
    "${EXECUTORCH_INSTALL_DIR}" "${DEPS_DIR}" EXECUTORCH_INSTALL_DIR DEPS_DIR

export TMPDIR="${WORK_DIR}/tmp"
export UV_CACHE_DIR="${WORK_DIR}/cache/uv"
export PIP_CACHE_DIR="${WORK_DIR}/cache/pip"
export XDG_CACHE_HOME="${WORK_DIR}/cache/xdg"
export MAX_JOBS="${JOBS}"
export CMAKE_BUILD_PARALLEL_LEVEL="${JOBS}"
export USE_KINETO="${USE_KINETO:-0}"

need_cmd curl
need_cmd git
need_cmd tar
need_cmd cmake
need_cmd ar
need_cmd file
if [[ "${BUILD_DEB}" -eq 1 ]]; then
    need_cmd dpkg-deb
fi

CMAKE_TARGET_ARGS=()
if [[ "${TARGET_ARCH}" == "x86_64" ]]; then
    need_cmd "${X86_64_C_COMPILER}"
    need_cmd "${X86_64_CXX_COMPILER}"
    need_cmd "${X86_64_AR}"
    need_cmd "${X86_64_RANLIB}"
    need_cmd "${X86_64_STRIP}"

    X86_64_C_TARGET="$("${X86_64_C_COMPILER}" -dumpmachine)"
    X86_64_CXX_TARGET="$("${X86_64_CXX_COMPILER}" -dumpmachine)"
    case "${X86_64_C_TARGET}:${X86_64_CXX_TARGET}" in
        x86_64*-linux-gnu*:x86_64*-linux-gnu*) ;;
        *)
            die "x86_64 compilers target ${X86_64_C_TARGET}/${X86_64_CXX_TARGET}; expected x86_64-linux-gnu"
            ;;
    esac

    CMAKE_TARGET_ARGS=(
        "-DCMAKE_TOOLCHAIN_FILE=${SCRIPT_DIR}/toolchains/x86_64-linux-gnu-gcc14.cmake"
        "-DPEK_EXECUTORCH_X86_64_C_COMPILER=${X86_64_C_COMPILER}"
        "-DPEK_EXECUTORCH_X86_64_CXX_COMPILER=${X86_64_CXX_COMPILER}"
        "-DPEK_EXECUTORCH_X86_64_AR=${X86_64_AR}"
        "-DPEK_EXECUTORCH_X86_64_RANLIB=${X86_64_RANLIB}"
        "-DPEK_EXECUTORCH_X86_64_STRIP=${X86_64_STRIP}"
    )
elif [[ "${TARGET_ARCH}" == "arm64" ]]; then
    need_cmd "${ARM_C_COMPILER}"
    need_cmd "${ARM_CXX_COMPILER}"
    need_cmd "${ARM_AR}"
    need_cmd "${ARM_RANLIB}"
    need_cmd "${ARM_STRIP}"

    ARM_C_TARGET="$("${ARM_C_COMPILER}" -dumpmachine)"
    ARM_CXX_TARGET="$("${ARM_CXX_COMPILER}" -dumpmachine)"
    case "${ARM_C_TARGET}:${ARM_CXX_TARGET}" in
        aarch64*-linux-gnu*:aarch64*-linux-gnu*) ;;
        *)
            die "Arm64 compilers target ${ARM_C_TARGET}/${ARM_CXX_TARGET}; expected aarch64-linux-gnu"
            ;;
    esac

    CMAKE_TARGET_ARGS=(
        "-DCMAKE_TOOLCHAIN_FILE=${SCRIPT_DIR}/toolchains/aarch64-linux-gnu-gcc14.cmake"
        "-DPEK_EXECUTORCH_ARM_C_COMPILER=${ARM_C_COMPILER}"
        "-DPEK_EXECUTORCH_ARM_CXX_COMPILER=${ARM_CXX_COMPILER}"
        "-DPEK_EXECUTORCH_ARM_AR=${ARM_AR}"
        "-DPEK_EXECUTORCH_ARM_RANLIB=${ARM_RANLIB}"
        "-DPEK_EXECUTORCH_ARM_STRIP=${ARM_STRIP}"
        "-DEXECUTORCH_XNNPACK_ENABLE_KLEIDI=ON"
    )
fi

if command -v ninja > /dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=(-G Ninja)
else
    need_cmd make
    CMAKE_GENERATOR_ARGS=()
fi

python_build_environment_is_exact() {
    "${VENV_DIR}/bin/python" - << 'PY'
import importlib.metadata

expected = {
    "pyyaml": "6.0.1",
    "torch": "2.12.0+cpu",
    "typing-extensions": "4.13.2",
}
bootstrap = {"pip", "setuptools"}
installed = {
    distribution.metadata["Name"].lower().replace("_", "-").replace(".", "-"):
    distribution.version
    for distribution in importlib.metadata.distributions()
}

if any(installed.get(name) != version for name, version in expected.items()):
    raise SystemExit(1)
if set(installed) - set(expected) - bootstrap:
    raise SystemExit(1)

import torchgen  # noqa: F401
import typing_extensions  # noqa: F401
import yaml  # noqa: F401
PY
}

create_venv() {
    log "Preparing Python ${PYTHON_VERSION} venv: ${VENV_DIR}"
    mkdir -p "${WORK_DIR}"

    if [[ -x "${VENV_DIR}/bin/python" ]] &&
        [[ "$("${VENV_DIR}/bin/python" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')" == "${PYTHON_VERSION}" ]] &&
        python_build_environment_is_exact; then
        log "Reusing exact Python build environment"
        return 0
    fi

    if [[ -e "${VENV_DIR}" ]]; then
        log "Replacing invalid Python venv"
        safe_rm_rf "${VENV_DIR}"
    fi

    if command -v uv > /dev/null 2>&1; then
        (cd "${WORK_DIR}" && uv venv --no-project --python "${PYTHON_VERSION}" "${VENV_DIR}")
    else
        local pybin
        pybin="$(command -v "python${PYTHON_VERSION}" || true)"
        [[ -n "${pybin}" ]] || die "Python ${PYTHON_VERSION} not found and uv is unavailable"
        "${pybin}" -m venv "${VENV_DIR}"
    fi

    "${VENV_DIR}/bin/python" - << 'PY'
import sys
if sys.version_info[:2] != (3, 11):
    raise SystemExit(f"ExecuTorch build requires Python 3.11, got {sys.version.split()[0]}")
PY
}

verify_executorch_archive() {
    local archive="$1"
    local actual

    if command -v sha256sum > /dev/null 2>&1; then
        actual="$(sha256sum "${archive}" | awk '{print $1}')"
    elif command -v shasum > /dev/null 2>&1; then
        actual="$(shasum -a 256 "${archive}" | awk '{print $1}')"
    else
        die "cannot verify archive checksum: missing sha256sum or shasum"
    fi

    [[ "${actual}" == "${EXECUTORCH_ARCHIVE_SHA256}" ]] ||
        die "ExecuTorch archive checksum mismatch: expected ${EXECUTORCH_ARCHIVE_SHA256}, got ${actual}"
}

prepare_executorch_source() {
    local archive="${DOWNLOAD_DIR}/executorch-v${EXECUTORCH_VERSION}.tar.gz"
    local extract_dir="${WORK_DIR}/executorch-${EXECUTORCH_VERSION}.extract"
    local extracted_source="${extract_dir}/executorch-${EXECUTORCH_VERSION}"

    log "Preparing ExecuTorch ${EXECUTORCH_VERSION} source: ${EXECUTORCH_DIR}"
    mkdir -p "$(dirname -- "${EXECUTORCH_DIR}")"
    mkdir -p "${DOWNLOAD_DIR}"

    if [[ ! -f "${archive}" ]]; then
        log "Downloading ExecuTorch ${EXECUTORCH_VERSION}: ${EXECUTORCH_ARCHIVE_URL}"
        curl --retry 5 --retry-all-errors -fL "${EXECUTORCH_ARCHIVE_URL}" -o "${archive}"
    else
        log "Using cached ExecuTorch archive: ${archive}"
    fi

    verify_executorch_archive "${archive}"

    if [[ -e "${EXECUTORCH_DIR}" ]]; then
        if [[ -f "${EXECUTORCH_DIR}/version.txt" ]] &&
            [[ "$(tr -d '[:space:]' < "${EXECUTORCH_DIR}/version.txt")" == "${EXECUTORCH_VERSION}" ]]; then
            log "Reusing existing ExecuTorch ${EXECUTORCH_VERSION} source"
            return 0
        fi

        die "${EXECUTORCH_DIR} exists but is not an ExecuTorch ${EXECUTORCH_VERSION} source tree"
    fi

    safe_rm_rf "${extract_dir}"
    mkdir -p "${extract_dir}"

    log "Extracting ExecuTorch ${EXECUTORCH_VERSION} archive"
    tar -xzf "${archive}" -C "${extract_dir}"

    [[ -d "${extracted_source}" ]] ||
        die "ExecuTorch archive did not contain expected directory: executorch-${EXECUTORCH_VERSION}"

    mv "${extracted_source}" "${EXECUTORCH_DIR}"
    safe_rm_rf "${extract_dir}"
}

source_externals_ready() {
    local required_paths=(
        third-party/json/CMakeLists.txt
        third-party/gflags/CMakeLists.txt
        third-party/flatbuffers/CMakeLists.txt
        third-party/flatcc/CMakeLists.txt
        backends/xnnpack/third-party/FXdiv/CMakeLists.txt
        backends/xnnpack/third-party/cpuinfo/CMakeLists.txt
        backends/xnnpack/third-party/pthreadpool/CMakeLists.txt
        backends/xnnpack/third-party/XNNPACK/CMakeLists.txt
    )

    local path
    for path in "${required_paths[@]}"; do
        if [[ ! -f "${EXECUTORCH_DIR}/${path}" ]]; then
            return 1
        fi
    done

    return 0
}

populate_executorch_submodules() {
    [[ -f "${EXECUTORCH_DIR}/.gitmodules" ]] ||
        die "ExecuTorch source is missing .gitmodules; cannot recover third-party sources"

    log "Populating ExecuTorch ${EXECUTORCH_VERSION} third-party sources from pinned tag metadata"
    if ! (
        cd "${EXECUTORCH_DIR}"

        if [[ ! -d .git ]]; then
            git init
        fi

        if git remote get-url origin > /dev/null 2>&1; then
            git remote set-url origin "${EXECUTORCH_GIT_URL}"
        else
            git remote add origin "${EXECUTORCH_GIT_URL}"
        fi

        git fetch --depth 1 origin "refs/tags/v${EXECUTORCH_VERSION}"
        local fetched_commit
        fetched_commit="$(git rev-parse FETCH_HEAD)"
        [[ "${fetched_commit}" == "${EXECUTORCH_TAG_COMMIT}" ]] ||
            die "ExecuTorch tag commit mismatch: expected ${EXECUTORCH_TAG_COMMIT}, got ${fetched_commit}"
        git reset --mixed "${EXECUTORCH_TAG_COMMIT}"
        git submodule sync --recursive
        git submodule update --init --recursive --force \
            third-party/json \
            third-party/gflags \
            third-party/flatbuffers \
            third-party/flatcc \
            backends/xnnpack/third-party/FP16 \
            backends/xnnpack/third-party/FXdiv \
            backends/xnnpack/third-party/XNNPACK \
            backends/xnnpack/third-party/cpuinfo \
            backends/xnnpack/third-party/pthreadpool
    ); then
        die "failed to populate ExecuTorch third-party sources"
    fi

    source_externals_ready || die "ExecuTorch third-party source population is incomplete"
    [[ -z "$(git -C "${EXECUTORCH_DIR}" status --porcelain --untracked-files=all)" ]] ||
        die "ExecuTorch source or pinned submodules contain local changes"
}

install_executorch_python_deps() {
    log "Installing the Python dependencies used by the selected CMake build"
    [[ -f "${PYTHON_BUILD_REQUIREMENTS}" ]] ||
        die "missing Python build requirements: ${PYTHON_BUILD_REQUIREMENTS}"

    if ! (
        cd "${WORK_DIR}"
        if command -v uv > /dev/null 2>&1; then
            uv pip install \
                --python "${VENV_DIR}/bin/python" \
                --no-cache \
                --no-deps \
                --no-index \
                --require-hashes \
                --requirements "${PYTHON_BUILD_REQUIREMENTS}"
        elif "${VENV_DIR}/bin/python" -m pip --version > /dev/null 2>&1; then
            "${VENV_DIR}/bin/python" -m pip install \
                --no-cache-dir \
                --no-deps \
                --no-index \
                --require-hashes \
                --requirement "${PYTHON_BUILD_REQUIREMENTS}"
        else
            exit 1
        fi
    ); then
        die "failed to install ExecuTorch Python build dependencies"
    fi

    python_build_environment_is_exact ||
        die "ExecuTorch Python build environment does not match its locked contract"
}

configure_and_build_executorch() {
    log "Configuring ExecuTorch CMake build"
    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        safe_rm_rf "${BUILD_DIR}"
    fi

    cmake -S "${EXECUTORCH_DIR}" -B "${BUILD_DIR}" "${CMAKE_GENERATOR_ARGS[@]}" \
        "${CMAKE_TARGET_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPYTHON_EXECUTABLE="${VENV_DIR}/bin/python" \
        -DPython_EXECUTABLE="${VENV_DIR}/bin/python" \
        -DEXECUTORCH_BUILD_PYTHON=OFF \
        -DEXECUTORCH_BUILD_TESTS=OFF \
        -DEXECUTORCH_BUILD_EXAMPLES=OFF \
        -DEXECUTORCH_BUILD_EXTENSION_MODULE=ON \
        -DEXECUTORCH_BUILD_EXTENSION_FLAT_TENSOR=ON \
        -DEXECUTORCH_BUILD_EXTENSION_TENSOR=ON \
        -DEXECUTORCH_BUILD_EXTENSION_DATA_LOADER=ON \
        -DEXECUTORCH_BUILD_EXTENSION_EVALUE_UTIL=OFF \
        -DEXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP=ON \
        -DEXECUTORCH_BUILD_EXTENSION_RUNNER_UTIL=OFF \
        -DEXECUTORCH_BUILD_PORTABLE_OPS=ON \
        -DEXECUTORCH_BUILD_EXECUTOR_RUNNER=OFF \
        -DEXECUTORCH_BUILD_XNNPACK=ON \
        -DEXECUTORCH_BUILD_XNNPACK_BACKEND=ON \
        -DEXECUTORCH_SELECT_ALL_OPS=ON \
        -DCMAKE_INSTALL_PREFIX="${EXECUTORCH_INSTALL_DIR}"

    log "Building ExecuTorch with ${JOBS} job(s)"
    cmake --build "${BUILD_DIR}" --parallel "${JOBS}" \
        --target "${EXECUTORCH_BUILD_TARGETS[@]}"
}

copy_built_libraries() {
    local libdir="${EXECUTORCH_INSTALL_DIR}/lib"
    local artifact
    local lib
    safe_rm_rf "${EXECUTORCH_INSTALL_DIR}"
    mkdir -p "${libdir}"

    log "Installing ExecuTorch to ${EXECUTORCH_INSTALL_DIR}"
    cmake --install "${BUILD_DIR}" --prefix "${EXECUTORCH_INSTALL_DIR}"

    log "Removing install metadata and test-only headers"
    find "${EXECUTORCH_INSTALL_DIR}" \
        -mindepth 1 -maxdepth 1 \
        ! -name include ! -name lib \
        -exec rm -rf -- {} +
    find "${EXECUTORCH_INSTALL_DIR}/include" \
        -type d \( \
            -name test -o \
            -name tests -o \
            -name testing -o \
            -name testing_util -o \
            -name test_utils \
        \) \
        -prune -exec rm -rf -- {} +

    log "Staging only the libraries linked by PEK"
    find "${libdir}" -mindepth 1 -delete
    for lib in "${EXECUTORCH_REQUIRED_LIBS[@]}"; do
        artifact="$(find "${BUILD_DIR}" -type f -name "${lib}" -print -quit)"
        [[ -n "${artifact}" ]] || die "built ExecuTorch library not found: ${lib}"
        cp -f "${artifact}" "${libdir}/"
    done
}

validate_staged_files() {
    log "Validating staged files for PEK"

    local required_headers=(
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/extension/module/module.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/extension/tensor/tensor_ptr.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/extension/tensor/tensor_ptr_maker.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/runtime/core/error.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/runtime/core/evalue.h"
        "${EXECUTORCH_INSTALL_DIR}/include/executorch/runtime/core/portable_type/c10/c10/util/irange.h"
    )

    local architecture_archive="${EXECUTORCH_INSTALL_DIR}/lib/libexecutorch.a"
    if [[ -f "${architecture_archive}" ]]; then
        local detected_arch
        detected_arch="$(archive_architecture "${architecture_archive}")"
        log "Detected ExecuTorch target architecture: ${detected_arch}"
        [[ "${detected_arch}" == "${TARGET_ARCH}" ]] ||
            die "ExecuTorch target mismatch: requested ${TARGET_ARCH}, built ${detected_arch}"

    fi

    local missing=0
    local path
    for path in "${required_headers[@]}"; do
        if [[ ! -e "${path}" ]]; then
            printf 'Missing required header/path: %s\n' "${path}" >&2
            missing=1
        fi
    done

    local lib
    for lib in "${EXECUTORCH_REQUIRED_LIBS[@]}"; do
        path="${EXECUTORCH_INSTALL_DIR}/lib/${lib}"
        if [[ ! -f "${path}" ]]; then
            printf 'Missing required library: %s\n' "${path}" >&2
            missing=1
        fi
    done

    if find "${EXECUTORCH_INSTALL_DIR}" \
        -mindepth 1 -maxdepth 1 \
        ! -name include ! -name lib \
        -print -quit | grep -q .; then
        die "ExecuTorch staging contains unexpected top-level content"
    fi
    if find "${EXECUTORCH_INSTALL_DIR}/include" \
        -type d \( \
            -name test -o \
            -name tests -o \
            -name testing -o \
            -name testing_util -o \
            -name test_utils \
        \) \
        -print -quit | grep -q .; then
        die "ExecuTorch staging contains test-only headers"
    fi

    [[ "${missing}" -eq 0 ]] || die "ExecuTorch staging is incomplete"
}

build_debian_package() {
    [[ "${BUILD_DEB}" -eq 1 ]] || return 0

    log "Creating ExecuTorch Debian package"
    "${SCRIPT_DIR}/package-executorch-1.3.1-deb.sh" \
        --executorch-dir "${EXECUTORCH_INSTALL_DIR}" \
        --output-dir "${DEB_OUTPUT_DIR}" \
        --revision "${DEB_REVISION}"
}

print_summary() {
    cat << EOF

ExecuTorch development files are ready.

ExecuTorch:
  ${EXECUTORCH_INSTALL_DIR}

Target architecture:
  ${TARGET_ARCH}
EOF

    if [[ "${BUILD_DEB}" -eq 1 ]]; then
        cat << EOF

Debian package output:
  ${DEB_OUTPUT_DIR}
EOF
    fi

    cat << EOF

Build PEK with:
  PEK_EXECUTORCH_ROOT=${EXECUTORCH_INSTALL_DIR} PEK_EXECUTORCH=enabled ./scripts/build-elements.sh debug
EOF
}

log "Project root: ${PROJECT_ROOT}"
log "Work dir: ${WORK_DIR}"
log "Deps dir: ${DEPS_DIR}"
log "Target architecture: ${TARGET_ARCH}"
log "ExecuTorch version: ${EXECUTORCH_VERSION}"
log "ExecuTorch archive URL: ${EXECUTORCH_ARCHIVE_URL}"
log "ExecuTorch git URL for submodules: ${EXECUTORCH_GIT_URL}"

if [[ "${CLEAN_WORK_DIR}" -eq 1 ]]; then
    log "Deleting work dir before starting: ${WORK_DIR}"
    safe_rm_rf "${WORK_DIR}" 1
fi

mkdir -p "${WORK_DIR}"
mkdir -p "${TMPDIR}" "${UV_CACHE_DIR}" "${PIP_CACHE_DIR}" "${XDG_CACHE_HOME}"
cd "${WORK_DIR}"

create_venv
prepare_executorch_source
populate_executorch_submodules
install_executorch_python_deps
configure_and_build_executorch
copy_built_libraries
validate_staged_files
build_debian_package
print_summary
