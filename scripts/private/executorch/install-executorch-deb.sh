#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

log() {
    printf '[executorch-deb-install] %s\n' "$*"
}

warn() {
    printf 'WARNING: %s\n' "$*" >&2
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" > /dev/null 2>&1 || die "missing required command: $1"
}

PACKAGE_NAME="libexecutorch-dev"
PACKAGE_VERSION="${EXECUTORCH_VERSION:-1.3.1}"
PACKAGE_REVISION="${EXECUTORCH_DEB_REVISION:-1}"
PACKAGE_DIR="${EXECUTORCH_DEB_PACKAGE_DIR:-/tmp/pek-executorch-packages}"
INSTALL_ROOT="${EXECUTORCH_DEB_INSTALL_ROOT:-/opt/pek-deps}"
ARTIFACTORY_SERVER="${EXECUTORCH_ARTIFACTORY_SERVER:-}"
ARTIFACTORY_REPOSITORY="${EXECUTORCH_ARTIFACTORY_REPOSITORY:-}"
ARTIFACTORY_DISTRIBUTION="${EXECUTORCH_ARTIFACTORY_DISTRIBUTION:-}"
ARTIFACTORY_COMPONENT="${EXECUTORCH_ARTIFACTORY_COMPONENT:-}"
ARTIFACTORY_USERNAME="${EXECUTORCH_ARTIFACTORY_USERNAME:-}"
ARTIFACTORY_PASSWORD="${EXECUTORCH_ARTIFACTORY_PASSWORD:-}"

apt_source_file=""
apt_auth_file=""
artifactory_attempted=0

cleanup_artifactory_state() {
    [[ -z "${apt_source_file}" ]] || rm -f "${apt_source_file}"
    [[ -z "${apt_auth_file}" ]] || rm -f "${apt_auth_file}"
    if [[ "${artifactory_attempted}" -eq 1 ]]; then
        rm -rf /var/lib/apt/lists/*
    fi
}
trap cleanup_artifactory_state EXIT

executorch_sdk_is_installed() {
    [[ -f "${INSTALL_ROOT}/executorch/lib/libexecutorch.a" ]] &&
        [[ -d "${INSTALL_ROOT}/executorch/include" ]] &&
        [[ -d "${INSTALL_ROOT}/libtorch/include" ]]
}

purge_invalid_installation() {
    dpkg --purge "${PACKAGE_NAME}" > /dev/null 2>&1 || true
}

install_local_package() {
    local package_path="$1"

    log "ExecuTorch source: local package ${package_path}"
    if dpkg --install "${package_path}" && executorch_sdk_is_installed; then
        log "Installed ${package_path}"
        return 0
    fi

    warn "Failed to install a valid ExecuTorch SDK from ${package_path}"
    purge_invalid_installation
    return 1
}

artifactory_is_configured() {
    [[ -n "${ARTIFACTORY_SERVER}" ]] &&
        [[ -n "${ARTIFACTORY_REPOSITORY}" ]] &&
        [[ -n "${ARTIFACTORY_DISTRIBUTION}" ]] &&
        [[ -n "${ARTIFACTORY_COMPONENT}" ]] &&
        [[ -n "${ARTIFACTORY_USERNAME}" ]] &&
        [[ -n "${ARTIFACTORY_PASSWORD}" ]]
}

prepare_artifactory_apt_config() {
    local package_arch="$1"
    local repository_url="$2"

    install -d -m 0755 /etc/apt/auth.conf.d /etc/apt/sources.list.d || return 1
    apt_source_file="$(mktemp /etc/apt/sources.list.d/executorch-artifactory.XXXXXX.list)" ||
        return 1
    apt_auth_file="$(mktemp /etc/apt/auth.conf.d/executorch-artifactory.XXXXXX.conf)" ||
        return 1

    printf 'deb [arch=%s trusted=yes] %s %s %s\n' \
        "${package_arch}" \
        "${repository_url}" \
        "${ARTIFACTORY_DISTRIBUTION}" \
        "${ARTIFACTORY_COMPONENT}" > "${apt_source_file}" || return 1
    printf 'machine %s\nlogin %s\npassword %s\n' \
        "${repository_url}" \
        "${ARTIFACTORY_USERNAME}" \
        "${ARTIFACTORY_PASSWORD}" > "${apt_auth_file}" || return 1
    chmod 0600 "${apt_auth_file}" || return 1
}

install_from_artifactory() {
    local package_arch="$1"
    local requested_package="$2"
    local artifactory_server
    local artifactory_repository
    local repository_url

    if ! artifactory_is_configured; then
        log "Artifactory is not fully configured for ${requested_package}"
        return 1
    fi

    artifactory_server="${ARTIFACTORY_SERVER%/}"
    case "${artifactory_server}" in
        https://*) ;;
        http://*) die "Artifactory server must use HTTPS" ;;
        *) artifactory_server="https://${artifactory_server}" ;;
    esac
    artifactory_repository="${ARTIFACTORY_REPOSITORY#/}"
    artifactory_repository="${artifactory_repository%/}"
    repository_url="${artifactory_server}/artifactory/${artifactory_repository}"

    artifactory_attempted=1
    prepare_artifactory_apt_config "${package_arch}" "${repository_url}" ||
        die "failed to create temporary Artifactory APT configuration"
    log "ExecuTorch source: Artifactory ${repository_url} ${ARTIFACTORY_DISTRIBUTION}/${ARTIFACTORY_COMPONENT}"

    if apt-get \
           -o "Dir::Etc::sourcelist=${apt_source_file}" \
           -o 'Dir::Etc::sourceparts=-' \
           -o 'APT::Get::List-Cleanup=0' \
           update &&
        apt-get \
            -o "Dir::Etc::sourcelist=${apt_source_file}" \
            -o 'Dir::Etc::sourceparts=-' \
            install -y --no-install-recommends \
            "${PACKAGE_NAME}=${PACKAGE_VERSION}-${PACKAGE_REVISION}" &&
        executorch_sdk_is_installed; then
        log "Installed ExecuTorch ${PACKAGE_VERSION}-${PACKAGE_REVISION} from Artifactory"
        return 0
    fi

    warn "Failed to install ${requested_package} from Artifactory"
    purge_invalid_installation
    return 1
}

need_cmd apt-get
need_cmd dpkg
need_cmd mktemp

package_arch="$(dpkg --print-architecture)"
package_filename="${PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_REVISION}-${package_arch}.deb"
local_package="${PACKAGE_DIR%/}/${package_filename}"

if [[ -f "${local_package}" ]]; then
    if install_local_package "${local_package}"; then
        exit 0
    fi
else
    log "Local ExecuTorch package not found: ${local_package}"
fi

if install_from_artifactory "${package_arch}" "${package_filename}"; then
    exit 0
fi

log "ExecuTorch source: unavailable; continuing without ExecuTorch support"
