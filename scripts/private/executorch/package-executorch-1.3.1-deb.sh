#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

usage() {
    cat << 'EOF'
Package a staged ExecuTorch 1.3.1 SDK for OPK as a Debian archive.

Usage:
  scripts/private/executorch/package-executorch-1.3.1-deb.sh [options]

Options:
  --executorch-dir DIR  Staged ExecuTorch SDK. Default: $OPK_PROJECT_ROOT/deps/executorch
  --libtorch-dir DIR    Staged libtorch compatibility headers. Default: $OPK_PROJECT_ROOT/deps/libtorch
  --legal-documentation-dir DIR
                        ExecuTorch and third-party licenses/copyright notices.
                        Default: $OPK_PROJECT_ROOT/deps/executorch-legal-documentation
  --output-dir DIR      Debian package output directory. Default: $OPK_PROJECT_ROOT/var
  --install-root DIR    Package installation root. Default: /opt/opk-deps
  --revision REV        Debian package revision. Default: 2
  --help                Show this help.

Environment:
  OPK_PROJECT_ROOT            OPK checkout root. Default: checkout containing this script.
  EXECUTORCH_SDK_DIR          Same as --executorch-dir.
  LIBTORCH_SDK_DIR            Same as --libtorch-dir.
  EXECUTORCH_LEGAL_DOCUMENTATION_DIR
                              Same as --legal-documentation-dir.
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
    local member
    local description

    IFS= read -r member < <(ar t "${archive}")
    [[ -n "${member}" ]] || die "cannot inspect empty or invalid archive: ${archive}"

    description="$(ar p "${archive}" "${member}" | file -b -)" ||
        die "cannot determine target architecture from ${archive}"
    case "${description}" in
        *x86-64* | *x86_64*)
            printf 'amd64\n'
            ;;
        *aarch64* | *AArch64* | *arm64* | *ARM64*)
            printf 'arm64\n'
            ;;
        *arm* | *Arm* | *ARM*)
            die "unsupported 32-bit ARM target in ${archive}; expected AArch64"
            ;;
        *)
            die "unsupported target architecture in ${archive}: ${description}"
            ;;
    esac
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
requested_project_root="${OPK_PROJECT_ROOT:-${SCRIPT_DIR}/../../..}"
[[ "${requested_project_root}" == /* ]] ||
    die "OPK_PROJECT_ROOT must be an absolute path: ${requested_project_root}"
[[ -d "${requested_project_root}" ]] ||
    die "OPK project root does not exist: ${requested_project_root}"
OPK_PROJECT_ROOT="$(cd -- "${requested_project_root}" && pwd -P)"
[[ -f "${OPK_PROJECT_ROOT}/development/meson.build" ]] ||
    die "OPK_PROJECT_ROOT is not an OPK checkout: ${OPK_PROJECT_ROOT}"
export OPK_PROJECT_ROOT

ORIGINAL_CWD="$(pwd -P)"
PACKAGE_NAME="libexecutorch-dev"
PACKAGE_VERSION="1.3.1"
EXECUTORCH_DIR="${EXECUTORCH_SDK_DIR:-${OPK_PROJECT_ROOT}/deps/executorch}"
LIBTORCH_DIR="${LIBTORCH_SDK_DIR:-${OPK_PROJECT_ROOT}/deps/libtorch}"
LEGAL_DOCUMENTATION_DIR="${EXECUTORCH_LEGAL_DOCUMENTATION_DIR:-${OPK_PROJECT_ROOT}/deps/executorch-legal-documentation}"
OUTPUT_DIR="${EXECUTORCH_DEB_OUTPUT_DIR:-${OPK_PROJECT_ROOT}/var}"
INSTALL_ROOT="${EXECUTORCH_DEB_INSTALL_ROOT:-/opt/opk-deps}"
PACKAGE_REVISION="${EXECUTORCH_DEB_REVISION:-2}"
PACKAGE_MAINTAINER="${EXECUTORCH_DEB_MAINTAINER:-Arm Limited}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --executorch-dir)
            [[ $# -ge 2 ]] || die "--executorch-dir requires a value"
            EXECUTORCH_DIR="$2"
            shift 2
            ;;
        --libtorch-dir)
            [[ $# -ge 2 ]] || die "--libtorch-dir requires a value"
            LIBTORCH_DIR="$2"
            shift 2
            ;;
        --legal-documentation-dir)
            [[ $# -ge 2 ]] || die "--legal-documentation-dir requires a value"
            LEGAL_DOCUMENTATION_DIR="$2"
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
[[ "${PACKAGE_REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] ||
    die "invalid Debian package revision: ${PACKAGE_REVISION}"
[[ -n "${PACKAGE_MAINTAINER}" ]] || die "Debian package maintainer must not be empty"

EXECUTORCH_DIR="$(resolve_path "${EXECUTORCH_DIR}")"
LIBTORCH_DIR="$(resolve_path "${LIBTORCH_DIR}")"
LEGAL_DOCUMENTATION_DIR="$(resolve_path "${LEGAL_DOCUMENTATION_DIR}")"
OUTPUT_DIR="$(resolve_path "${OUTPUT_DIR}")"

need_cmd ar
need_cmd cp
need_cmd dpkg-deb
need_cmd file
need_cmd find

required_paths=(
    "${EXECUTORCH_DIR}/include/executorch/extension/module/module.h"
    "${EXECUTORCH_DIR}/include/executorch/extension/tensor/tensor_ptr_maker.h"
    "${EXECUTORCH_DIR}/include/executorch/runtime/core/error.h"
    "${EXECUTORCH_DIR}/lib/libexecutorch.a"
    "${LIBTORCH_DIR}/include"
    "${LEGAL_DOCUMENTATION_DIR}"
)

for path in "${required_paths[@]}"; do
    [[ -e "${path}" ]] || die "missing required SDK path: ${path}"
done

[[ -n "$(find "${LEGAL_DOCUMENTATION_DIR}" -type f -print -quit)" ]] ||
    die "ExecuTorch and third-party licenses/copyright notices are missing: ${LEGAL_DOCUMENTATION_DIR}"
[[ -z "$(find "${LEGAL_DOCUMENTATION_DIR}" -type l -print -quit)" ]] ||
    die "ExecuTorch legal documentation must not contain symlinks: ${LEGAL_DOCUMENTATION_DIR}"

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
if [[ "${DEBIAN_ARCH}" == "arm64" ]]; then
    required_libs+=(libkleidiai.a)
fi

for lib in "${required_libs[@]}"; do
    [[ -f "${EXECUTORCH_DIR}/lib/${lib}" ]] ||
        die "missing required ExecuTorch library: ${EXECUTORCH_DIR}/lib/${lib}"
done

PACKAGE_FILE="${OUTPUT_DIR}/${PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_REVISION}-${DEBIAN_ARCH}.deb"
PACKAGE_ROOT="${OUTPUT_DIR}/.${PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_REVISION}-${DEBIAN_ARCH}.package"
PAYLOAD_ROOT="${PACKAGE_ROOT}${INSTALL_ROOT}"

rm -rf "${PACKAGE_ROOT}"
mkdir -p \
    "${PACKAGE_ROOT}/DEBIAN" \
    "${PAYLOAD_ROOT}/executorch" \
    "${PAYLOAD_ROOT}/libtorch" \
    "${PAYLOAD_ROOT}/executorch-legal-documentation"

log "Copying ExecuTorch SDK into ${INSTALL_ROOT}/executorch"
cp -a "${EXECUTORCH_DIR}/." "${PAYLOAD_ROOT}/executorch/"
log "Copying libtorch compatibility headers into ${INSTALL_ROOT}/libtorch"
cp -a "${LIBTORCH_DIR}/include" "${PAYLOAD_ROOT}/libtorch/"
log "Copying ExecuTorch and third-party licenses/copyright notices into ${INSTALL_ROOT}/executorch-legal-documentation"
cp -a "${LEGAL_DOCUMENTATION_DIR}/." "${PAYLOAD_ROOT}/executorch-legal-documentation/"

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
Description: ExecuTorch ${PACKAGE_VERSION} development SDK for OPK
 Target-specific ExecuTorch libraries, headers, and libtorch compatibility
 headers required to compile and run OPK with the ExecuTorch backend.
EOF

chmod 0755 "${PACKAGE_ROOT}/DEBIAN"
chmod 0644 "${PACKAGE_ROOT}/DEBIAN/control"
mkdir -p "${OUTPUT_DIR}"

log "Building ${PACKAGE_FILE}"
dpkg-deb --root-owner-group --build "${PACKAGE_ROOT}" "${PACKAGE_FILE}"
rm -rf "${PACKAGE_ROOT}"

log "Created ${PACKAGE_FILE}"
printf '%s\n' "${PACKAGE_FILE}"
