#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
Upload an ExecuTorch 1.3.1 Debian package to the OPK Artifactory repository.

Usage:
  scripts/private/executorch/upload-executorch-1.3.1-deb.sh [options] PACKAGE

Arguments:
  PACKAGE  Debian package produced by package-executorch-1.3.1-deb.sh.

Options:
  --server URL          JFrog server URL.
                        Default: https://artifactory.arm.com:443
  --repository NAME     Debian repository key.
                        Default: ai-expkits-internal.opk-deb
  --distribution NAME   Debian distribution. Default: trixie
  --component NAME      Debian component. Default: main
  --skip-reindex        Do not request Debian repository metadata recalculation.
  --help                Show this help.

Environment:
  EXECUTORCH_ARTIFACTORY_SERVER        Same as --server.
  EXECUTORCH_ARTIFACTORY_REPOSITORY    Same as --repository.
  EXECUTORCH_ARTIFACTORY_DISTRIBUTION  Same as --distribution.
  EXECUTORCH_ARTIFACTORY_COMPONENT     Same as --component.
  EXECUTORCH_ARTIFACTORY_USERNAME      Required Artifactory username.
  EXECUTORCH_ARTIFACTORY_PASSWORD      Required Artifactory access token.

The package name, version, and architecture are read from its Debian control
metadata. The artifact is uploaded under pool/ with its complete .deb filename.
EOF
}

log() {
    printf '[executorch-deb-upload] %s\n' "$*"
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" > /dev/null 2>&1 || die "missing required command: $1"
}

ARTIFACTORY_SERVER="${EXECUTORCH_ARTIFACTORY_SERVER:-https://artifactory.arm.com:443}"
ARTIFACTORY_REPOSITORY="${EXECUTORCH_ARTIFACTORY_REPOSITORY:-ai-expkits-internal.opk-deb}"
ARTIFACTORY_DISTRIBUTION="${EXECUTORCH_ARTIFACTORY_DISTRIBUTION:-trixie}"
ARTIFACTORY_COMPONENT="${EXECUTORCH_ARTIFACTORY_COMPONENT:-main}"
ARTIFACTORY_USERNAME="${EXECUTORCH_ARTIFACTORY_USERNAME:-}"
ARTIFACTORY_PASSWORD="${EXECUTORCH_ARTIFACTORY_PASSWORD:-}"
PACKAGE_FILE=""
REINDEX=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server)
            [[ $# -ge 2 ]] || die "--server requires a value"
            ARTIFACTORY_SERVER="$2"
            shift 2
            ;;
        --repository)
            [[ $# -ge 2 ]] || die "--repository requires a value"
            ARTIFACTORY_REPOSITORY="$2"
            shift 2
            ;;
        --distribution)
            [[ $# -ge 2 ]] || die "--distribution requires a value"
            ARTIFACTORY_DISTRIBUTION="$2"
            shift 2
            ;;
        --component)
            [[ $# -ge 2 ]] || die "--component requires a value"
            ARTIFACTORY_COMPONENT="$2"
            shift 2
            ;;
        --skip-reindex)
            REINDEX=0
            shift
            ;;
        --help | -h)
            usage
            exit 0
            ;;
        --)
            shift
            [[ $# -eq 1 ]] || die "exactly one package path is required"
            PACKAGE_FILE="$1"
            shift
            ;;
        -*)
            die "unknown option: $1"
            ;;
        *)
            [[ -z "${PACKAGE_FILE}" ]] || die "exactly one package path is required"
            PACKAGE_FILE="$1"
            shift
            ;;
    esac
done

[[ -n "${PACKAGE_FILE}" ]] || die "a package path is required"
[[ -f "${PACKAGE_FILE}" ]] || die "package does not exist: ${PACKAGE_FILE}"
[[ "${ARTIFACTORY_SERVER}" == https://* ]] || die "Artifactory server must use HTTPS"
ARTIFACTORY_SERVER="${ARTIFACTORY_SERVER%/}"
server_authority="${ARTIFACTORY_SERVER#https://}"
[[ "${server_authority}" != */* ]] || die "Artifactory server must not contain a path"
artifactory_host="${server_authority%%:*}"
[[ -n "${artifactory_host}" ]] || die "invalid Artifactory server: ${ARTIFACTORY_SERVER}"

ARTIFACTORY_REPOSITORY="${ARTIFACTORY_REPOSITORY#/}"
ARTIFACTORY_REPOSITORY="${ARTIFACTORY_REPOSITORY%/}"
[[ "${ARTIFACTORY_REPOSITORY}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] ||
    die "invalid Artifactory repository key: ${ARTIFACTORY_REPOSITORY}"
[[ "${ARTIFACTORY_DISTRIBUTION}" =~ ^[A-Za-z0-9][A-Za-z0-9._+~-]*$ ]] ||
    die "invalid Debian distribution: ${ARTIFACTORY_DISTRIBUTION}"
[[ "${ARTIFACTORY_COMPONENT}" =~ ^[A-Za-z0-9][A-Za-z0-9._+~-]*$ ]] ||
    die "invalid Debian component: ${ARTIFACTORY_COMPONENT}"
[[ -n "${ARTIFACTORY_USERNAME}" ]] || die "EXECUTORCH_ARTIFACTORY_USERNAME is required"
[[ -n "${ARTIFACTORY_PASSWORD}" ]] || die "EXECUTORCH_ARTIFACTORY_PASSWORD is required"
[[ ! "${ARTIFACTORY_USERNAME}" =~ [[:space:]] ]] ||
    die "Artifactory username must not contain whitespace"
[[ ! "${ARTIFACTORY_PASSWORD}" =~ [[:space:]] ]] ||
    die "Artifactory access token must not contain whitespace"

need_cmd curl
need_cmd dpkg-deb
need_cmd mktemp

package_name="$(dpkg-deb --field "${PACKAGE_FILE}" Package)"
package_version="$(dpkg-deb --field "${PACKAGE_FILE}" Version)"
package_architecture="$(dpkg-deb --field "${PACKAGE_FILE}" Architecture)"

[[ "${package_name}" == "libexecutorch-dev" ]] ||
    die "unexpected Debian package name: ${package_name}"
[[ "${package_version}" == 1.3.1-* ]] ||
    die "unexpected ExecuTorch package version: ${package_version}"
case "${package_architecture}" in
    amd64 | arm64) ;;
    *) die "unsupported ExecuTorch package architecture: ${package_architecture}" ;;
esac

package_filename="$(basename -- "${PACKAGE_FILE}")"
expected_filename="${package_name}-${package_version}-${package_architecture}.deb"
[[ "${package_filename}" == "${expected_filename}" ]] ||
    die "package filename must be ${expected_filename}, got ${package_filename}"

repository_url="${ARTIFACTORY_SERVER}/artifactory/${ARTIFACTORY_REPOSITORY}"
artifact_url="${repository_url}/pool/${package_filename};deb.distribution=${ARTIFACTORY_DISTRIBUTION};deb.component=${ARTIFACTORY_COMPONENT};deb.architecture=${package_architecture}"
reindex_url="${ARTIFACTORY_SERVER}/artifactory/api/deb/reindex/${ARTIFACTORY_REPOSITORY}"

umask 077
auth_file="$(mktemp)"
cleanup() {
    rm -f "${auth_file}"
}
trap cleanup EXIT
printf 'machine %s\nlogin %s\npassword %s\n' \
    "${artifactory_host}" \
    "${ARTIFACTORY_USERNAME}" \
    "${ARTIFACTORY_PASSWORD}" > "${auth_file}"

artifactory_curl() {
    curl \
        --fail-with-body \
        --netrc-file "${auth_file}" \
        --show-error \
        --silent \
        "$@"
}

log "Uploading ${package_filename} as ${ARTIFACTORY_DISTRIBUTION}/${ARTIFACTORY_COMPONENT}/${package_architecture}"
artifactory_curl --request PUT --upload-file "${PACKAGE_FILE}" "${artifact_url}"
printf '\n'

if [[ "${REINDEX}" -eq 1 ]]; then
    log "Recalculating Debian metadata for ${ARTIFACTORY_REPOSITORY}"
    artifactory_curl --request POST "${reindex_url}"
    printf '\n'
fi

log "Uploaded ${repository_url}/pool/${package_filename}"
