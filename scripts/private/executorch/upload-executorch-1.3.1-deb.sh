#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Upload an ExecuTorch 1.3.1 Debian package to the PEK Artifactory repository.

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
The publishing identity must have Deploy/Cache permission without
Delete/Overwrite permission so Artifactory enforces write-once publication.
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

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACTORY_SERVER="${EXECUTORCH_ARTIFACTORY_SERVER:-https://artifactory.arm.com:443}"
ARTIFACTORY_REPOSITORY="${EXECUTORCH_ARTIFACTORY_REPOSITORY:-ai-expkits-internal.opk-deb}"
ARTIFACTORY_DISTRIBUTION="${EXECUTORCH_ARTIFACTORY_DISTRIBUTION:-trixie}"
ARTIFACTORY_COMPONENT="${EXECUTORCH_ARTIFACTORY_COMPONENT:-main}"
ARTIFACTORY_USERNAME="${EXECUTORCH_ARTIFACTORY_USERNAME:-}"
ARTIFACTORY_PASSWORD="${EXECUTORCH_ARTIFACTORY_PASSWORD:-}"
PACKAGE_FILE=""

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

need_cmd awk
need_cmd chmod
need_cmd cp
need_cmd curl
need_cmd dpkg-deb
need_cmd mktemp
need_cmd sha256sum
need_cmd sleep

package_filename="$(basename -- "${PACKAGE_FILE}")"
umask 077
auth_file="$(mktemp)"
response_headers="$(mktemp)"
validation_dir="$(mktemp -d)"
cleanup() {
    rm -f "${auth_file}" "${response_headers}"
    rm -rf "${validation_dir}"
}
trap cleanup EXIT
package_snapshot="${validation_dir}/${package_filename}"
cp -- "${PACKAGE_FILE}" "${package_snapshot}"
chmod 0400 "${package_snapshot}"
PACKAGE_FILE="${package_snapshot}"

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
package_revision="${package_version#1.3.1-}"

expected_filename="${package_name}-${package_version}-${package_architecture}.deb"
[[ "${package_filename}" == "${expected_filename}" ]] ||
    die "package filename must be ${expected_filename}, got ${package_filename}"

repository_url="${ARTIFACTORY_SERVER}/artifactory/${ARTIFACTORY_REPOSITORY}"
artifact_path_url="${repository_url}/pool/${package_filename}"
artifact_url="${artifact_path_url};deb.distribution=${ARTIFACTORY_DISTRIBUTION};deb.component=${ARTIFACTORY_COMPONENT};deb.architecture=${package_architecture}"

printf 'machine %s\nlogin %s\npassword %s\n' \
    "${artifactory_host}" \
    "${ARTIFACTORY_USERNAME}" \
    "${ARTIFACTORY_PASSWORD}" > "${auth_file}"

mkdir -p "${validation_dir}/control" "${validation_dir}/payload"
dpkg-deb --control "${PACKAGE_FILE}" "${validation_dir}/control"
control_entries="$(
    find "${validation_dir}/control" -mindepth 1 -maxdepth 1 -printf '%f %y\n'
)"
[[ "${control_entries}" == "control f" ]] ||
    die "package control archive must contain only one regular control file"
dpkg-deb --extract "${PACKAGE_FILE}" "${validation_dir}/payload"
while IFS= read -r path; do
    case "${path}" in
        "${validation_dir}/payload/opt" | \
            "${validation_dir}/payload/opt/pek-deps" | \
            "${validation_dir}/payload/opt/pek-deps/executorch" | \
            "${validation_dir}/payload/opt/pek-deps/executorch/"*) ;;
        *) die "package contains content outside /opt/pek-deps/executorch: ${path}" ;;
    esac
done < <(find "${validation_dir}/payload" -mindepth 1 -print)
"${SCRIPT_DIR}/package-executorch-1.3.1-deb.sh" \
    --executorch-dir "${validation_dir}/payload/opt/pek-deps/executorch" \
    --output-dir "${validation_dir}" \
    --revision "${package_revision}" \
    --expected-architecture "${package_architecture}" \
    --validate-only

artifactory_curl() {
    curl \
        --fail-with-body \
        --netrc-file "${auth_file}" \
        --proto '=https' \
        --proto-redir '=https' \
        --show-error \
        --silent \
        "$@"
}

inspect_artifact() {
    http_status="$(
        curl \
            --head \
            --location \
            --netrc-file "${auth_file}" \
            --output "${response_headers}" \
            --proto '=https' \
            --proto-redir '=https' \
            --show-error \
            --silent \
            --write-out '%{http_code}' \
            "${artifact_path_url}"
    )" || die "failed to inspect the existing Artifactory package"

    remote_sha256=""
    if [[ "${http_status}" == "200" ]]; then
        remote_sha256="$(
            awk '
                tolower($1) == "x-checksum-sha256:" {
                    gsub(/\r/, "", $2)
                    print tolower($2)
                }
            ' "${response_headers}"
        )"
        [[ -n "${remote_sha256}" && "${remote_sha256}" != *$'\n'* ]] ||
            die "existing Artifactory package has no unique SHA-256 header"
        [[ "${remote_sha256}" =~ ^[0-9a-f]{64}$ ]] ||
            die "existing Artifactory package has an invalid SHA-256 header"
    fi
}

local_sha256="$(sha256sum "${PACKAGE_FILE}" | awk '{print $1}')"
inspect_artifact

case "${http_status}" in
    200)
        if [[ "${local_sha256}" == "${remote_sha256}" ]]; then
            log "Package already exists with identical content: ${artifact_path_url}"
        else
            die "refusing to overwrite ${artifact_path_url}; increment the Debian package revision"
        fi
        ;;
    404)
        log "Uploading ${package_filename} as ${ARTIFACTORY_DISTRIBUTION}/${ARTIFACTORY_COMPONENT}/${package_architecture}"
        if ! artifactory_curl \
            --request PUT \
            --header "X-Checksum-Sha256: ${local_sha256}" \
            --upload-file "${PACKAGE_FILE}" \
            "${artifact_url}"; then
            log "Upload failed; checking whether another publisher created the package"
            inspect_artifact
            if [[ "${http_status}" != "200" || "${remote_sha256}" != "${local_sha256}" ]]; then
                die "failed to publish ${artifact_path_url} without overwriting another package"
            fi
        fi
        printf '\n'
        ;;
    *)
        die "failed to inspect ${artifact_path_url}: HTTP ${http_status}"
        ;;
esac

readback_dir="${validation_dir}/readback"
readback_log="${validation_dir}/readback.log"
published_package="${readback_dir}/${package_filename}"
readback_verified=0
for attempt in {1..12}; do
    rm -rf "${readback_dir}"
    mkdir -p "${readback_dir}"
    if EXECUTORCH_ARTIFACTORY_SERVER="${ARTIFACTORY_SERVER}" \
        EXECUTORCH_ARTIFACTORY_REPOSITORY="${ARTIFACTORY_REPOSITORY}" \
        EXECUTORCH_ARTIFACTORY_DISTRIBUTION="${ARTIFACTORY_DISTRIBUTION}" \
        EXECUTORCH_ARTIFACTORY_COMPONENT="${ARTIFACTORY_COMPONENT}" \
        EXECUTORCH_ARTIFACTORY_USERNAME="${ARTIFACTORY_USERNAME}" \
        EXECUTORCH_ARTIFACTORY_PASSWORD="${ARTIFACTORY_PASSWORD}" \
        EXECUTORCH_DEB_ARCHITECTURE="${package_architecture}" \
        EXECUTORCH_DEB_REVISION="${package_revision}" \
        EXECUTORCH_DEB_PACKAGE_DIR="${validation_dir}/no-local-package" \
        EXECUTORCH_DEB_FETCH_DIR="${readback_dir}" \
        EXECUTORCH_REQUIRED=1 \
        "${SCRIPT_DIR}/install-executorch-deb.sh" > "${readback_log}" 2>&1 &&
        [[ "$(sha256sum "${published_package}" | awk '{print $1}')" == "${local_sha256}" ]]; then
        readback_verified=1
        break
    fi
    if [[ "${attempt}" -lt 12 ]]; then
        sleep 10
    fi
done
if [[ "${readback_verified}" -ne 1 ]]; then
    cat "${readback_log}" >&2
    die "published package did not appear in the signed Debian repository metadata"
fi

log "Published and verified ${repository_url}/pool/${package_filename}"
