#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Package a staged ExecuTorch 1.3.1 SDK for PEK as a Debian archive.

Usage:
  scripts/private/executorch/package-executorch-1.3.1-deb.sh [options]

Options:
  --executorch-dir DIR  Staged ExecuTorch SDK. Default: /work/deps/executorch
  --output-dir DIR      Debian package output directory. Default: /work/var
  --install-root DIR    Package installation root. Default: /opt/pek-deps
  --revision REV        Debian package revision. Default: 2
  --expected-architecture ARCH
                        Require the detected Debian architecture to match ARCH.
  --validate-only       Validate the staged SDK without creating a package.
  --help                Show this help.

Environment:
  EXECUTORCH_SDK_DIR          Same as --executorch-dir.
  EXECUTORCH_DEB_OUTPUT_DIR   Same as --output-dir.
  EXECUTORCH_DEB_INSTALL_ROOT Same as --install-root.
  EXECUTORCH_DEB_REVISION     Same as --revision.
  EXECUTORCH_DEB_MAINTAINER   Debian Maintainer field. Default: Arm Limited

The package architecture is detected from libexecutorch.a. The resulting file
is named libexecutorch-dev-1.3.1-<revision>-<architecture>.deb.
EOF
}

log() {
    printf '[executorch-deb] %s\n' "$*"
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

archive_architecture() {
    local archive="$1"
    local archive_members
    local detected_architecture=""
    local header_output
    local machine
    local member_architecture
    local -a machines=()
    local -a members=()

    [[ "$(head -c 8 "${archive}")" == "!<arch>" ]] ||
        die "ExecuTorch library must be a self-contained regular archive: ${archive}"
    archive_members="$(ar t "${archive}")" ||
        die "cannot inspect invalid archive: ${archive}"
    mapfile -t members <<< "${archive_members}"
    [[ "${#members[@]}" -gt 0 && -n "${members[0]}" ]] ||
        die "cannot inspect empty archive: ${archive}"

    header_output="$(LC_ALL=C readelf --file-header "${archive}" 2>&1)" ||
        die "cannot inspect ELF members in ${archive}: ${header_output}"
    mapfile -t machines < <(
        sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p' <<< "${header_output}"
    )
    [[ "${#machines[@]}" -eq "${#members[@]}" ]] ||
        die "archive must contain only inspectable ELF object members: ${archive}"

    for machine in "${machines[@]}"; do
        case "${machine}" in
            "Advanced Micro Devices X86-64")
                member_architecture="amd64"
                ;;
            AArch64)
                member_architecture="arm64"
                ;;
            ARM)
                die "unsupported 32-bit ARM target in ${archive}; expected AArch64"
                ;;
            *)
                die "unsupported target architecture in ${archive}: ${machine}"
                ;;
        esac
        if [[ -n "${detected_architecture}" &&
            "${member_architecture}" != "${detected_architecture}" ]]; then
            die "archive contains mixed target architectures: ${archive}"
        fi
        detected_architecture="${member_architecture}"
    done

    printf '%s\n' "${detected_architecture}"
}

ORIGINAL_CWD="$(pwd -P)"
PACKAGE_NAME="libexecutorch-dev"
PACKAGE_VERSION="1.3.1"
# ExecuTorch v1.3.1 tag commit e2f18eb23c45bd22ca332b0b8b49a81de304b472.
PACKAGE_SOURCE_DATE_EPOCH="1780002276"
EXECUTORCH_DIR="${EXECUTORCH_SDK_DIR:-/work/deps/executorch}"
OUTPUT_DIR="${EXECUTORCH_DEB_OUTPUT_DIR:-/work/var}"
INSTALL_ROOT="${EXECUTORCH_DEB_INSTALL_ROOT:-/opt/pek-deps}"
PACKAGE_REVISION="${EXECUTORCH_DEB_REVISION:-2}"
PACKAGE_MAINTAINER="${EXECUTORCH_DEB_MAINTAINER:-Arm Limited}"
EXPECTED_ARCHITECTURE=""
VALIDATE_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --executorch-dir)
            [[ $# -ge 2 ]] || die "--executorch-dir requires a value"
            EXECUTORCH_DIR="$2"
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || die "--output-dir requires a value"
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --install-root)
            [[ $# -ge 2 ]] || die "--install-root requires a value"
            INSTALL_ROOT="$2"
            shift 2
            ;;
        --revision)
            [[ $# -ge 2 ]] || die "--revision requires a value"
            PACKAGE_REVISION="$2"
            shift 2
            ;;
        --expected-architecture)
            [[ $# -ge 2 ]] || die "--expected-architecture requires a value"
            EXPECTED_ARCHITECTURE="$2"
            shift 2
            ;;
        --validate-only)
            VALIDATE_ONLY=1
            shift
            ;;
        --help | -h)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

[[ "${INSTALL_ROOT}" == /* ]] || die "--install-root must be an absolute path"
[[ "${INSTALL_ROOT}" != "/" ]] || die "--install-root must not be /"
INSTALL_ROOT="${INSTALL_ROOT%/}"
case "/${INSTALL_ROOT#/}/" in
    */../* | */./*)
        die "--install-root must not contain . or .. path components"
        ;;
esac
[[ "${PACKAGE_REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] ||
    die "invalid Debian package revision: ${PACKAGE_REVISION}"
[[ -n "${PACKAGE_MAINTAINER}" ]] || die "Debian package maintainer must not be empty"
case "${EXPECTED_ARCHITECTURE}" in
    "" | amd64 | arm64) ;;
    *) die "unsupported expected architecture: ${EXPECTED_ARCHITECTURE}" ;;
esac

EXECUTORCH_DIR="$(resolve_path "${EXECUTORCH_DIR}")"
OUTPUT_DIR="$(resolve_path "${OUTPUT_DIR}")"

need_cmd ar
need_cmd cp
need_cmd dpkg-deb
need_cmd find
need_cmd head
need_cmd readelf
need_cmd sed
need_cmd touch

required_paths=(
    "${EXECUTORCH_DIR}/include/executorch/extension/module/module.h"
    "${EXECUTORCH_DIR}/include/executorch/extension/tensor/tensor_ptr.h"
    "${EXECUTORCH_DIR}/include/executorch/extension/tensor/tensor_ptr_maker.h"
    "${EXECUTORCH_DIR}/include/executorch/runtime/core/error.h"
    "${EXECUTORCH_DIR}/include/executorch/runtime/core/evalue.h"
    "${EXECUTORCH_DIR}/include/executorch/runtime/core/portable_type/c10/c10/util/irange.h"
    "${EXECUTORCH_DIR}/lib/libexecutorch.a"
)

for path in "${required_paths[@]}"; do
    [[ -f "${path}" ]] || die "missing required SDK file: ${path}"
done

if find "${EXECUTORCH_DIR}" \
    -mindepth 1 -maxdepth 1 \
    ! -name include ! -name lib \
    -print -quit | grep -q .; then
    die "ExecuTorch SDK contains unexpected top-level content"
fi
if find "${EXECUTORCH_DIR}/include" \
    -type d \( \
        -name test -o \
        -name tests -o \
        -name testing -o \
        -name testing_util -o \
        -name test_utils \
    \) \
    -print -quit | grep -q .; then
    die "ExecuTorch SDK contains test-only headers"
fi

required_libs=(
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
    libXNNPACK.a
    libxnnpack_backend.a
    libxnnpack-microkernels-prod.a
)

DEBIAN_ARCH="$(archive_architecture "${EXECUTORCH_DIR}/lib/libexecutorch.a")"
if [[ -n "${EXPECTED_ARCHITECTURE}" && "${DEBIAN_ARCH}" != "${EXPECTED_ARCHITECTURE}" ]]; then
    die "ExecuTorch SDK architecture ${DEBIAN_ARCH} does not match expected ${EXPECTED_ARCHITECTURE}"
fi
if [[ "${DEBIAN_ARCH}" == "arm64" ]]; then
    required_libs+=(libkleidiai.a)
fi

for lib in "${required_libs[@]}"; do
    library_path="${EXECUTORCH_DIR}/lib/${lib}"
    [[ -f "${library_path}" ]] ||
        die "missing required ExecuTorch library: ${library_path}"
    if [[ "${lib}" == "libexecutorch.a" ]]; then
        library_architecture="${DEBIAN_ARCH}"
    else
        library_architecture="$(archive_architecture "${library_path}")"
    fi
    [[ "${library_architecture}" == "${DEBIAN_ARCH}" ]] ||
        die "ExecuTorch library ${library_path} targets ${library_architecture}; expected ${DEBIAN_ARCH}"
done

while IFS= read -r path; do
    candidate="$(basename -- "${path}")"
    expected=0
    for lib in "${required_libs[@]}"; do
        if [[ "${candidate}" == "${lib}" ]]; then
            expected=1
            break
        fi
    done
    [[ "${expected}" -eq 1 ]] ||
        die "unexpected ExecuTorch library: ${path}"
done < <(find "${EXECUTORCH_DIR}/lib" -mindepth 1 -maxdepth 1 -print)

required_include_entries=(cpuinfo.h executorch fxdiv.h pthreadpool.h xnnpack.h)
if [[ "${DEBIAN_ARCH}" == "arm64" ]]; then
    required_include_entries+=(kai)
fi
for entry in cpuinfo.h fxdiv.h pthreadpool.h xnnpack.h; do
    [[ -f "${EXECUTORCH_DIR}/include/${entry}" ]] ||
        die "missing required ExecuTorch top-level header: ${EXECUTORCH_DIR}/include/${entry}"
done
[[ -d "${EXECUTORCH_DIR}/include/executorch" ]] ||
    die "missing required ExecuTorch header directory: ${EXECUTORCH_DIR}/include/executorch"
if [[ "${DEBIAN_ARCH}" == "arm64" ]]; then
    [[ -d "${EXECUTORCH_DIR}/include/kai" ]] ||
        die "missing required ExecuTorch header directory: ${EXECUTORCH_DIR}/include/kai"
fi
while IFS= read -r path; do
    candidate="$(basename -- "${path}")"
    expected=0
    for entry in "${required_include_entries[@]}"; do
        if [[ "${candidate}" == "${entry}" ]]; then
            expected=1
            break
        fi
    done
    [[ "${expected}" -eq 1 ]] ||
        die "unexpected ExecuTorch top-level header path: ${path}"
done < <(find "${EXECUTORCH_DIR}/include" -mindepth 1 -maxdepth 1 -print)
if find "${EXECUTORCH_DIR}" \
    \( ! -type d ! -type f -o -type f \( \
        -name '*.cmake' -o \
        -name '*.o' -o \
        -name '*.obj' -o \
        -name '*.pc' -o \
        -name '*.so' -o \
        -name '*.so.*' \
    \) \) \
    -print -quit | grep -q .; then
    die "ExecuTorch SDK contains unsupported file types, build metadata, objects, or shared libraries"
fi

if [[ "${VALIDATE_ONLY}" -eq 1 ]]; then
    log "Validated ${EXECUTORCH_DIR}"
    exit 0
fi

PACKAGE_FILE="${OUTPUT_DIR}/${PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_REVISION}-${DEBIAN_ARCH}.deb"
PACKAGE_ROOT="${OUTPUT_DIR}/.${PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_REVISION}-${DEBIAN_ARCH}.package"
PAYLOAD_ROOT="${PACKAGE_ROOT}/${INSTALL_ROOT#/}"
[[ "${PAYLOAD_ROOT}" == "${PACKAGE_ROOT}/"* ]] ||
    die "package payload must remain inside its staging directory"

rm -rf "${PACKAGE_ROOT}"
mkdir -p \
    "${PACKAGE_ROOT}/DEBIAN" \
    "${PAYLOAD_ROOT}/executorch"

log "Copying ExecuTorch SDK into ${INSTALL_ROOT}/executorch"
cp -a "${EXECUTORCH_DIR}/." "${PAYLOAD_ROOT}/executorch/"

find "${PACKAGE_ROOT}" -type d -exec chmod 0755 {} +
find "${PAYLOAD_ROOT}" -type f -exec chmod 0644 {} +
find "${PAYLOAD_ROOT}/executorch/lib" -type f -name '*.so*' -exec chmod 0755 {} +

cat > "${PACKAGE_ROOT}/DEBIAN/control" << EOF
Package: ${PACKAGE_NAME}
Version: ${PACKAGE_VERSION}-${PACKAGE_REVISION}
Section: libdevel
Priority: optional
Architecture: ${DEBIAN_ARCH}
Maintainer: ${PACKAGE_MAINTAINER}
Description: ExecuTorch ${PACKAGE_VERSION} development SDK for PEK
 Target-specific ExecuTorch libraries and headers required to compile and run
 PEK with the ExecuTorch backend.
EOF

chmod 0755 "${PACKAGE_ROOT}/DEBIAN"
chmod 0644 "${PACKAGE_ROOT}/DEBIAN/control"
mkdir -p "${OUTPUT_DIR}"
find "${PACKAGE_ROOT}" \
    -exec touch \
    --no-dereference \
    --date="@${PACKAGE_SOURCE_DATE_EPOCH}" \
    {} +

log "Building ${PACKAGE_FILE}"
SOURCE_DATE_EPOCH="${PACKAGE_SOURCE_DATE_EPOCH}" \
    dpkg-deb --root-owner-group --build "${PACKAGE_ROOT}" "${PACKAGE_FILE}"
rm -rf "${PACKAGE_ROOT}"

log "Created ${PACKAGE_FILE}"
printf '%s\n' "${PACKAGE_FILE}"
