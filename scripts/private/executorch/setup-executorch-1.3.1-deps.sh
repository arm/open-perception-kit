#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Build and stage ExecuTorch 1.3.1 development files for OPK.

Usage:
  scripts/private/executorch/setup-executorch-1.3.1-deps.sh <work-dir> [options]

Example:
  scripts/private/executorch/setup-executorch-1.3.1-deps.sh var/executorch-1.3.1-build
  scripts/private/executorch/setup-executorch-1.3.1-deps.sh var/executorch-1.3.1-arm-build --target-arch arm

Options:
  --work-dir DIR          Directory used for source, venv, downloads, build, temp, and caches.
  --deps-dir DIR          Root dependency staging directory. Default: $OPK_PROJECT_ROOT/deps
  --target-arch ARCH      Build target: x86_64 (default) or arm (AArch64 Linux GNU).
  --executorch-url URL    ExecuTorch source archive URL. Default: official v1.3.1 tarball.
  --executorch-git-url URL
                          Git URL used only to recover pinned submodule commits.
  --executorch-sha256 SHA Expected SHA-256 of the source archive. Optional.
  --jobs N                Build parallelism. Default: calculated from available CPU and memory.
  --deb-output-dir DIR    Debian package output directory. Default: $OPK_PROJECT_ROOT/var
  --deb-revision REV      Debian package revision. Default: 2
  --skip-deb              Stage files without creating a Debian package.
  --clean-build           Delete and recreate the CMake build directory.
  --clean-work-dir        Delete and recreate the entire work directory.
  --help                  Show this help.

Environment:
  OPK_PROJECT_ROOT        OPK checkout root. Default: checkout containing this script.
  DEPS_DIR                Same as --deps-dir.
  EXECUTORCH_TARGET_ARCH  Same as --target-arch.
  EXECUTORCH_ARCHIVE_URL  Same as --executorch-url.
  EXECUTORCH_GIT_URL      Same as --executorch-git-url.
  EXECUTORCH_ARCHIVE_SHA256
                          Same as --executorch-sha256.
  EXECUTORCH_INSTALL_DIR  Default: $DEPS_DIR/executorch
  LIBTORCH_INSTALL_DIR    Default: $DEPS_DIR/libtorch
  VENV_DIR                Default: $WORK_DIR/.venv
  PYTHON_VERSION          Default: 3.11
  JOBS                    Same as --jobs.
  CCACHE_DIR              Compiler cache directory. Default: $WORK_DIR/cache/ccache
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
  EXECUTORCH_LEGAL_DOCUMENTATION_DIR
                          Default: $DEPS_DIR/executorch-legal-documentation
  LIBTORCH_URL            Optional libtorch zip URL. If unset, torch headers are
                          copied from the ExecuTorch Python venv when available.

Output:
  $DEPS_DIR/executorch/include
  $DEPS_DIR/executorch/lib
  $DEPS_DIR/libtorch/include
  $DEPS_DIR/executorch-legal-documentation
  $OPK_PROJECT_ROOT/var/libexecutorch-dev-1.3.1-<revision>-<architecture>.deb

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

calculate_default_jobs() {
    local cpu_jobs
    local memory_jobs
    local available_kib
    local available_bytes
    local cgroup_available
    local quota
    local period
    local quota_jobs

    cpu_jobs="$(nproc 2> /dev/null || getconf _NPROCESSORS_ONLN 2> /dev/null || printf '1\n')"
    [[ "${cpu_jobs}" =~ ^[1-9][0-9]*$ ]] || cpu_jobs=1

    if read -r quota period < /sys/fs/cgroup/cpu.max 2> /dev/null &&
        [[ "${quota}" =~ ^[0-9]+$ ]] && [[ "${period}" =~ ^[1-9][0-9]*$ ]]; then
        quota_jobs=$(((quota + period - 1) / period))
        if ((quota_jobs < cpu_jobs)); then
            cpu_jobs="${quota_jobs}"
        fi
    fi

    available_kib="$(awk '/^MemAvailable:/ {print $2; exit}' /proc/meminfo 2> /dev/null || true)"
    if [[ "${available_kib}" =~ ^[0-9]+$ ]]; then
        available_bytes=$((available_kib * 1024))
    else
        available_bytes=$((2 * 1024 * 1024 * 1024))
    fi

    if [[ -r /sys/fs/cgroup/memory.max && -r /sys/fs/cgroup/memory.current ]]; then
        local memory_max
        local memory_current
        memory_max="$(< /sys/fs/cgroup/memory.max)"
        memory_current="$(< /sys/fs/cgroup/memory.current)"
        if [[ "${memory_max}" =~ ^[0-9]+$ ]] && [[ "${memory_current}" =~ ^[0-9]+$ ]] &&
            ((memory_max > memory_current)); then
            cgroup_available=$((memory_max - memory_current))
            if ((cgroup_available < available_bytes)); then
                available_bytes="${cgroup_available}"
            fi
        fi
    fi

    # ExecuTorch's generated C++ kernels are memory-heavy. Budget 2 GiB per
    # compiler process to avoid trading parallelism for swapping or OOM kills.
    memory_jobs=$((available_bytes / (2 * 1024 * 1024 * 1024)))
    if ((memory_jobs < 1)); then
        memory_jobs=1
    fi
    if ((memory_jobs < cpu_jobs)); then
        printf '%s\n' "${memory_jobs}"
    else
        printf '%s\n' "${cpu_jobs}"
    fi
}

resolve_path() {
    local path="$1"

    case "${path}" in
        /*) ;;
        *) path="${ORIGINAL_CWD}/${path}" ;;
    esac

    command -v realpath > /dev/null 2>&1 || die "missing required command: realpath"
    realpath -m -- "${path}"
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
requested_project_root="${OPK_PROJECT_ROOT:-${SCRIPT_DIR}/../../..}"
[[ "${requested_project_root}" == /* ]] ||
    die "OPK_PROJECT_ROOT must be an absolute path: ${requested_project_root}"
[[ -d "${requested_project_root}" ]] ||
    die "OPK project root does not exist: ${requested_project_root}"
PROJECT_ROOT="$(cd -- "${requested_project_root}" && pwd -P)"
[[ -f "${PROJECT_ROOT}/development/meson.build" ]] ||
    die "OPK_PROJECT_ROOT is not an OPK checkout: ${PROJECT_ROOT}"
OPK_PROJECT_ROOT="${PROJECT_ROOT}"
export OPK_PROJECT_ROOT
ORIGINAL_CWD="$(pwd -P)"

WORK_DIR=""
DEPS_DIR="${DEPS_DIR:-${OPK_PROJECT_ROOT}/deps}"
EXECUTORCH_VERSION="1.3.1"
EXECUTORCH_ARCHIVE_URL="${EXECUTORCH_ARCHIVE_URL:-https://github.com/pytorch/executorch/archive/refs/tags/v1.3.1.tar.gz}"
EXECUTORCH_GIT_URL="${EXECUTORCH_GIT_URL:-https://github.com/pytorch/executorch.git}"
EXECUTORCH_ARCHIVE_SHA256="${EXECUTORCH_ARCHIVE_SHA256:-}"
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"
JOBS="${JOBS:-}"
if [[ -n "${JOBS}" ]]; then
    JOBS_SOURCE="environment"
else
    JOBS_SOURCE="automatic"
fi
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
DEB_OUTPUT_DIR="${EXECUTORCH_DEB_OUTPUT_DIR:-${OPK_PROJECT_ROOT}/var}"
DEB_REVISION="${EXECUTORCH_DEB_REVISION:-2}"
BUILD_DEB=1
CLEAN_BUILD=0
CLEAN_WORK_DIR=0
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
        --executorch-sha256)
            [[ $# -ge 2 ]] || die "--executorch-sha256 requires a value"
            EXECUTORCH_ARCHIVE_SHA256="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"
            JOBS_SOURCE="command line"
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
        --clean-build)
            CLEAN_BUILD=1
            shift
            ;;
        --clean-work-dir)
            CLEAN_WORK_DIR=1
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
if [[ -z "${JOBS}" ]]; then
    JOBS="$(calculate_default_jobs)"
fi
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "jobs must be a positive integer: ${JOBS}"
if [[ "${BUILD_DEB}" -eq 1 ]]; then
    [[ "${DEB_REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] ||
        die "invalid Debian package revision: ${DEB_REVISION}"
fi

WORK_DIR="$(resolve_path "${WORK_DIR}")"
DEPS_DIR="$(resolve_path "${DEPS_DIR}")"

EXECUTORCH_DIR="${EXECUTORCH_DIR:-${WORK_DIR}/executorch}"
BUILD_DIR="${BUILD_DIR:-${EXECUTORCH_DIR}/build}"
VENV_DIR="${VENV_DIR:-${WORK_DIR}/.venv}"
EXECUTORCH_INSTALL_DIR="${EXECUTORCH_INSTALL_DIR:-${DEPS_DIR}/executorch}"
LIBTORCH_INSTALL_DIR="${LIBTORCH_INSTALL_DIR:-${DEPS_DIR}/libtorch}"
LEGAL_DOCUMENTATION_DIR="${EXECUTORCH_LEGAL_DOCUMENTATION_DIR:-${DEPS_DIR}/executorch-legal-documentation}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-${WORK_DIR}/downloads}"
LIBTORCH_URL="${LIBTORCH_URL:-}"

EXECUTORCH_DIR="$(resolve_path "${EXECUTORCH_DIR}")"
BUILD_DIR="$(resolve_path "${BUILD_DIR}")"
VENV_DIR="$(resolve_path "${VENV_DIR}")"
EXECUTORCH_INSTALL_DIR="$(resolve_path "${EXECUTORCH_INSTALL_DIR}")"
LIBTORCH_INSTALL_DIR="$(resolve_path "${LIBTORCH_INSTALL_DIR}")"
LEGAL_DOCUMENTATION_DIR="$(resolve_path "${LEGAL_DOCUMENTATION_DIR}")"
DOWNLOAD_DIR="$(resolve_path "${DOWNLOAD_DIR}")"
DEB_OUTPUT_DIR="$(resolve_path "${DEB_OUTPUT_DIR}")"

export TMPDIR="${WORK_DIR}/tmp"
export UV_CACHE_DIR="${WORK_DIR}/cache/uv"
export PIP_CACHE_DIR="${WORK_DIR}/cache/pip"
export XDG_CACHE_HOME="${WORK_DIR}/cache/xdg"
export CCACHE_DIR="${CCACHE_DIR:-${WORK_DIR}/cache/ccache}"
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
        "-DOPK_EXECUTORCH_X86_64_C_COMPILER=${X86_64_C_COMPILER}"
        "-DOPK_EXECUTORCH_X86_64_CXX_COMPILER=${X86_64_CXX_COMPILER}"
        "-DOPK_EXECUTORCH_X86_64_AR=${X86_64_AR}"
        "-DOPK_EXECUTORCH_X86_64_RANLIB=${X86_64_RANLIB}"
        "-DOPK_EXECUTORCH_X86_64_STRIP=${X86_64_STRIP}"
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
        "-DOPK_EXECUTORCH_ARM_C_COMPILER=${ARM_C_COMPILER}"
        "-DOPK_EXECUTORCH_ARM_CXX_COMPILER=${ARM_CXX_COMPILER}"
        "-DOPK_EXECUTORCH_ARM_AR=${ARM_AR}"
        "-DOPK_EXECUTORCH_ARM_RANLIB=${ARM_RANLIB}"
        "-DOPK_EXECUTORCH_ARM_STRIP=${ARM_STRIP}"
        "-DEXECUTORCH_XNNPACK_ENABLE_KLEIDI=ON"
    )
fi

if command -v ninja > /dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=(-G Ninja)
else
    need_cmd make
    CMAKE_GENERATOR_ARGS=()
fi

CCACHE_EXECUTABLE="$(command -v ccache || true)"
CMAKE_LAUNCHER_ARGS=()
if [[ -n "${CCACHE_EXECUTABLE}" ]]; then
    CMAKE_LAUNCHER_ARGS=(
        "-DCMAKE_C_COMPILER_LAUNCHER=${CCACHE_EXECUTABLE}"
        "-DCMAKE_CXX_COMPILER_LAUNCHER=${CCACHE_EXECUTABLE}"
    )
fi

create_venv() {
    log "Preparing Python ${PYTHON_VERSION} venv: ${VENV_DIR}"
    mkdir -p "${WORK_DIR}"

    if [[ -x "${VENV_DIR}/bin/python" ]] &&
        "${VENV_DIR}/bin/python" -c \
            'import sys; expected = tuple(map(int, sys.argv[1].split("."))); raise SystemExit(sys.version_info[:2] != expected)' \
            "${PYTHON_VERSION}"; then
        log "Reusing existing Python ${PYTHON_VERSION} venv"
    elif [[ -e "${VENV_DIR}" ]]; then
        die "${VENV_DIR} exists but is not a Python ${PYTHON_VERSION} venv; rerun with --clean-work-dir or remove it"
    elif command -v uv > /dev/null 2>&1; then
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

verify_executorch_archive() {
    local archive="$1"
    local actual

    [[ -n "${EXECUTORCH_ARCHIVE_SHA256}" ]] || return 0

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

    if [[ -e "${EXECUTORCH_DIR}" ]]; then
        if [[ -f "${EXECUTORCH_DIR}/version.txt" ]] &&
            [[ "$(tr -d '[:space:]' < "${EXECUTORCH_DIR}/version.txt")" == "${EXECUTORCH_VERSION}" ]]; then
            log "Reusing existing ExecuTorch ${EXECUTORCH_VERSION} source"
            return 0
        fi

        die "${EXECUTORCH_DIR} exists but is not an ExecuTorch ${EXECUTORCH_VERSION} source tree"
    fi

    if [[ ! -f "${archive}" ]]; then
        log "Downloading ExecuTorch ${EXECUTORCH_VERSION}: ${EXECUTORCH_ARCHIVE_URL}"
        curl --retry 5 --retry-all-errors -fL "${EXECUTORCH_ARCHIVE_URL}" -o "${archive}"
    else
        log "Using cached ExecuTorch archive: ${archive}"
    fi

    verify_executorch_archive "${archive}"

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
    if source_externals_ready; then
        log "ExecuTorch third-party sources are already populated"
        return 0
    fi

    [[ -f "${EXECUTORCH_DIR}/.gitmodules" ]] || # agent-static-analysis: allow-generated-path (external source archive)
        die "ExecuTorch source is missing .gitmodules; cannot recover third-party sources" # agent-static-analysis: allow-generated-path (external source archive)

    log "Populating ExecuTorch ${EXECUTORCH_VERSION} third-party sources from pinned tag metadata"
    (   
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
        git reset --mixed FETCH_HEAD
        git submodule sync --recursive
        git submodule update --init --recursive \
            third-party/json \
            third-party/gflags \
            third-party/flatbuffers \
            third-party/flatcc \
            backends/xnnpack/third-party/FP16 \
            backends/xnnpack/third-party/FXdiv \
            backends/xnnpack/third-party/XNNPACK \
            backends/xnnpack/third-party/cpuinfo \
            backends/xnnpack/third-party/pthreadpool
    )

    source_externals_ready || die "ExecuTorch third-party source population is incomplete"
}

install_executorch_python_deps() {
    # shellcheck disable=SC1091
    source "${VENV_DIR}/bin/activate"

    local state_file="${VENV_DIR}/.executorch-install-state"
    local expected_state
    expected_state="$(
        "${VENV_DIR}/bin/python" \
            "${SCRIPT_DIR}/executorch-python-install-state.py" \
            "${EXECUTORCH_DIR}" "${EXECUTORCH_VERSION}"
    )"

    if [[ -f "${state_file}" ]] &&
        [[ "$(tr -d '[:space:]' < "${state_file}")" == "${expected_state}" ]] &&
        "${VENV_DIR}/bin/python" \
            "${SCRIPT_DIR}/validate-executorch-python-install.py" \
            "${EXECUTORCH_VERSION}"; then
        log "Reusing installed ExecuTorch Python/build dependencies"
        return 0
    fi

    log "Installing ExecuTorch Python/build dependencies"
    (cd "${EXECUTORCH_DIR}" && ./install_executorch.sh)
    printf '%s\n' "${expected_state}" > "${state_file}"
}

configure_and_build_executorch() {
    log "Configuring ExecuTorch CMake build"
    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        safe_rm_rf "${BUILD_DIR}"
    fi

    cmake -S "${EXECUTORCH_DIR}" -B "${BUILD_DIR}" "${CMAKE_GENERATOR_ARGS[@]}" \
        "${CMAKE_TARGET_ARGS[@]}" \
        "${CMAKE_LAUNCHER_ARGS[@]}" \
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

stage_legal_documentation() {
    log "Staging ExecuTorch and third-party licenses/copyright notices to ${LEGAL_DOCUMENTATION_DIR}"
    safe_rm_rf "${LEGAL_DOCUMENTATION_DIR}"
    mkdir -p "${LEGAL_DOCUMENTATION_DIR}"

    local source_path
    local relative_path
    while IFS= read -r -d '' source_path; do
        relative_path="${source_path#"${EXECUTORCH_DIR}/"}"
        mkdir -p "${LEGAL_DOCUMENTATION_DIR}/$(dirname -- "${relative_path}")"
        cp "${source_path}" "${LEGAL_DOCUMENTATION_DIR}/${relative_path}"
    done < <(
        find "${EXECUTORCH_DIR}" \
            \( -path "${EXECUTORCH_DIR}/.git" -o -path "${BUILD_DIR}" \) -prune -o \
            -type f \
            \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'NOTICE*' -o -iname 'COPYRIGHT*' \) \
            -print0
    )

    [[ -n "$(find "${LEGAL_DOCUMENTATION_DIR}" -type f -print -quit)" ]] ||
        die "ExecuTorch source contains no packageable licenses/copyright notices"
}

validate_staged_files() {
    log "Validating staged files for OPK"

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
    )

    local architecture_archive="${EXECUTORCH_INSTALL_DIR}/lib/libexecutorch.a"
    if [[ -f "${architecture_archive}" ]]; then
        local detected_arch
        detected_arch="$(archive_architecture "${architecture_archive}")"
        log "Detected ExecuTorch target architecture: ${detected_arch}"
        [[ "${detected_arch}" == "${TARGET_ARCH}" ]] ||
            die "ExecuTorch target mismatch: requested ${TARGET_ARCH}, built ${detected_arch}"

        if [[ "${detected_arch}" == "arm64" ]]; then
            required_libs+=(libkleidiai.a)
        fi
    fi

    required_libs+=(
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

build_debian_package() {
    [[ "${BUILD_DEB}" -eq 1 ]] || return 0

    log "Creating ExecuTorch Debian package"
    "${SCRIPT_DIR}/package-executorch-1.3.1-deb.sh" \
        --executorch-dir "${EXECUTORCH_INSTALL_DIR}" \
        --libtorch-dir "${LIBTORCH_INSTALL_DIR}" \
        --legal-documentation-dir "${LEGAL_DOCUMENTATION_DIR}" \
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

libtorch compatibility headers:
  ${LIBTORCH_INSTALL_DIR}

ExecuTorch and third-party licenses/copyright notices:
  ${LEGAL_DOCUMENTATION_DIR}
EOF

    if [[ "${BUILD_DEB}" -eq 1 ]]; then
        cat << EOF

Debian package output:
  ${DEB_OUTPUT_DIR}
EOF
    fi

    cat << EOF

Build OPK with:
  OPK_EXECUTORCH_ROOT=${EXECUTORCH_INSTALL_DIR} OPK_LIBTORCH_ROOT=${LIBTORCH_INSTALL_DIR} OPK_EXECUTORCH=enabled ./scripts/build.sh debug
EOF
}

log "Project root: ${PROJECT_ROOT}"
log "Work dir: ${WORK_DIR}"
log "Deps dir: ${DEPS_DIR}"
log "Target architecture: ${TARGET_ARCH}"
log "ExecuTorch version: ${EXECUTORCH_VERSION}"
log "ExecuTorch archive URL: ${EXECUTORCH_ARCHIVE_URL}"
log "ExecuTorch git URL for submodules: ${EXECUTORCH_GIT_URL}"
log "Build parallelism: ${JOBS} job(s) (${JOBS_SOURCE})"
if [[ -n "${CCACHE_EXECUTABLE}" ]]; then
    log "Compiler cache: ${CCACHE_EXECUTABLE} (${CCACHE_DIR})"
else
    log "Compiler cache: disabled (ccache not found)"
fi

if [[ "${CLEAN_WORK_DIR}" -eq 1 ]]; then
    log "Deleting work dir before starting: ${WORK_DIR}"
    safe_rm_rf "${WORK_DIR}" 1
fi

mkdir -p "${WORK_DIR}"
mkdir -p "${TMPDIR}" "${UV_CACHE_DIR}" "${PIP_CACHE_DIR}" "${XDG_CACHE_HOME}"
if [[ -n "${CCACHE_EXECUTABLE}" ]]; then
    mkdir -p "${CCACHE_DIR}"
fi
cd "${WORK_DIR}"

create_venv
prepare_executorch_source
populate_executorch_submodules
install_executorch_python_deps
configure_and_build_executorch
copy_built_libraries
stage_libtorch_headers
stage_legal_documentation
validate_staged_files
build_debian_package
print_summary
