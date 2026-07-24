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

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_NAME="libexecutorch-dev"
PACKAGE_VERSION="1.3.1"
PACKAGE_REVISION="${EXECUTORCH_DEB_REVISION:-2}"
PACKAGE_ARCHITECTURE="${EXECUTORCH_DEB_ARCHITECTURE:-}"
EXECUTORCH_REQUIRED="${EXECUTORCH_REQUIRED:-0}"
PACKAGE_DIR="${EXECUTORCH_DEB_PACKAGE_DIR:-/tmp/pek-executorch-packages}"
FETCH_DIR="${EXECUTORCH_DEB_FETCH_DIR:-}"
INSTALL_ROOT="${EXECUTORCH_DEB_INSTALL_ROOT:-/opt/pek-deps}"
ARTIFACTORY_SERVER="${EXECUTORCH_ARTIFACTORY_SERVER:-}"
ARTIFACTORY_REPOSITORY="${EXECUTORCH_ARTIFACTORY_REPOSITORY:-}"
ARTIFACTORY_DISTRIBUTION="${EXECUTORCH_ARTIFACTORY_DISTRIBUTION:-}"
ARTIFACTORY_COMPONENT="${EXECUTORCH_ARTIFACTORY_COMPONENT:-}"
ARTIFACTORY_USERNAME="${EXECUTORCH_ARTIFACTORY_USERNAME:-}"
ARTIFACTORY_PASSWORD="${EXECUTORCH_ARTIFACTORY_PASSWORD:-}"
ARTIFACTORY_USERNAME_FILE="${EXECUTORCH_ARTIFACTORY_USERNAME_FILE:-}"
ARTIFACTORY_PASSWORD_FILE="${EXECUTORCH_ARTIFACTORY_PASSWORD_FILE:-}"
ARTIFACTORY_PUBLIC_KEY="${SCRIPT_DIR}/artifactory-debian-public.asc"
ARTIFACTORY_KEY_FINGERPRINT="190281B95926DE6B8DA2788CEAC1DE22E29E9596" # pragma: allowlist secret

case "${EXECUTORCH_REQUIRED}" in
    0 | 1) ;;
    *) die "EXECUTORCH_REQUIRED must be 0 or 1" ;;
esac
[[ "${PACKAGE_REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] ||
    die "invalid Debian package revision: ${PACKAGE_REVISION}"

read_secret_file() {
    local path="$1"

    [[ -n "${path}" && -s "${path}" ]] || return 1
    cat -- "${path}"
}

artifactory_download_dir=""

cleanup_artifactory_state() {
    [[ -z "${artifactory_download_dir}" ]] || rm -rf "${artifactory_download_dir}"
}
trap cleanup_artifactory_state EXIT

executorch_sdk_is_installed() {
    local required_paths=(
        "${INSTALL_ROOT}/executorch/include/executorch/extension/module/module.h"
        "${INSTALL_ROOT}/executorch/include/executorch/extension/tensor/tensor_ptr.h"
        "${INSTALL_ROOT}/executorch/include/executorch/extension/tensor/tensor_ptr_maker.h"
        "${INSTALL_ROOT}/executorch/include/executorch/runtime/core/error.h"
        "${INSTALL_ROOT}/executorch/include/executorch/runtime/core/evalue.h"
        "${INSTALL_ROOT}/executorch/include/executorch/runtime/core/portable_type/c10/c10/util/irange.h"
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
        libXNNPACK.a
        libxnnpack_backend.a
        libxnnpack-microkernels-prod.a
    )
    local path
    local lib

    if [[ "$(dpkg --print-architecture)" == "arm64" ]]; then
        required_libs+=(libkleidiai.a)
    fi

    for path in "${required_paths[@]}"; do
        [[ -f "${path}" ]] || return 1
    done
    for lib in "${required_libs[@]}"; do
        [[ -f "${INSTALL_ROOT}/executorch/lib/${lib}" ]] || return 1
    done
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

validate_package_metadata() {
    local package_path="$1"
    local package_arch="$2"
    local expected_version="${PACKAGE_VERSION}-${PACKAGE_REVISION}"

    [[ "$(dpkg-deb --field "${package_path}" Package)" == "${PACKAGE_NAME}" ]] &&
        [[ "$(dpkg-deb --field "${package_path}" Version)" == "${expected_version}" ]] &&
        [[ "$(dpkg-deb --field "${package_path}" Architecture)" == "${package_arch}" ]]
}

verify_file() {
    local path="$1"
    local expected_sha256="$2"
    local expected_size="$3"
    local actual_sha256
    local actual_size

    [[ "${expected_sha256}" =~ ^[0-9a-f]{64}$ ]] || return 1
    [[ "${expected_size}" =~ ^[0-9]+$ ]] || return 1
    actual_sha256="$(sha256sum "${path}" | awk '{print $1}')"
    actual_size="$(stat -c '%s' "${path}")"
    [[ "${actual_sha256}" == "${expected_sha256}" ]] &&
        [[ "${actual_size}" == "${expected_size}" ]]
}

artifactory_curl() {
    local auth_file="$1"
    shift

    curl \
        --fail-with-body \
        --location \
        --netrc-file "${auth_file}" \
        --proto '=https' \
        --proto-redir '=https' \
        --show-error \
        --silent \
        "$@"
}

download_from_artifactory() {
    local package_arch="$1"
    local requested_package="$2"
    local destination="$3"
    local artifactory_server
    local artifactory_repository
    local artifactory_host
    local auth_file
    local expected_index_sha256
    local expected_index_size
    local expected_package_sha256
    local expected_package_size
    local extra_field
    local index_record
    local key_record
    local key_fingerprint
    local key_fingerprint_count
    local key_pub_count
    local metadata_url
    local package_index_path
    local package_path
    local package_record
    local package_url
    local repository_url
    local server_authority
    local signature_record
    local signature_status
    local signer_fingerprint
    local signer_primary_fingerprint

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
    server_authority="${artifactory_server#https://}"
    [[ "${server_authority}" =~ ^[A-Za-z0-9][A-Za-z0-9.-]*(:[0-9]+)?$ ]] ||
        die "Artifactory server must contain only an HTTPS authority"
    artifactory_host="${server_authority%%:*}"

    artifactory_repository="${ARTIFACTORY_REPOSITORY#/}"
    artifactory_repository="${artifactory_repository%/}"
    [[ "${artifactory_repository}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] ||
        die "invalid Artifactory repository key: ${artifactory_repository}"
    [[ "${ARTIFACTORY_DISTRIBUTION}" =~ ^[A-Za-z0-9][A-Za-z0-9._+~-]*$ ]] ||
        die "invalid Debian distribution: ${ARTIFACTORY_DISTRIBUTION}"
    [[ "${ARTIFACTORY_COMPONENT}" =~ ^[A-Za-z0-9][A-Za-z0-9._+~-]*$ ]] ||
        die "invalid Debian component: ${ARTIFACTORY_COMPONENT}"
    [[ ! "${ARTIFACTORY_USERNAME}" =~ [[:space:]] ]] ||
        die "Artifactory username must not contain whitespace"
    [[ ! "${ARTIFACTORY_PASSWORD}" =~ [[:space:]] ]] ||
        die "Artifactory access token must not contain whitespace"
    [[ -f "${ARTIFACTORY_PUBLIC_KEY}" ]] ||
        die "missing Artifactory Debian public key: ${ARTIFACTORY_PUBLIC_KEY}"

    repository_url="${artifactory_server}/artifactory/${artifactory_repository}"
    metadata_url="${repository_url}/dists/${ARTIFACTORY_DISTRIBUTION}"
    package_index_path="${ARTIFACTORY_COMPONENT}/binary-${package_arch}/Packages"

    log "ExecuTorch source: Artifactory ${repository_url} ${ARTIFACTORY_DISTRIBUTION}/${ARTIFACTORY_COMPONENT}"

    umask 077
    artifactory_download_dir="$(mktemp -d)"
    auth_file="${artifactory_download_dir}/netrc"
    printf 'machine %s\nlogin %s\npassword %s\n' \
        "${artifactory_host}" \
        "${ARTIFACTORY_USERNAME}" \
        "${ARTIFACTORY_PASSWORD}" > "${auth_file}"
    chmod 0600 "${auth_file}"

    artifactory_curl "${auth_file}" \
        --output "${artifactory_download_dir}/Release" \
        "${metadata_url}/Release" || return 1
    artifactory_curl "${auth_file}" \
        --output "${artifactory_download_dir}/Release.gpg" \
        "${metadata_url}/Release.gpg" || return 1

    install -d -m 0700 "${artifactory_download_dir}/gnupg"
    if ! key_record="$(
        gpg \
            --batch \
            --no-options \
            --homedir "${artifactory_download_dir}/gnupg" \
            --with-colons \
            --fingerprint \
            --import-options show-only \
            --import "${ARTIFACTORY_PUBLIC_KEY}" 2> /dev/null |
            awk -F: '
                $1 == "pub" {
                    pub_count++
                    primary_fingerprint_pending = 1
                    next
                }
                primary_fingerprint_pending && $1 == "fpr" {
                    primary_fingerprint_count++
                    primary_fingerprint = $10
                    primary_fingerprint_pending = 0
                }
                END {
                    print pub_count, primary_fingerprint_count, primary_fingerprint
                }
            '
    )"; then
        die "failed to inspect the Artifactory Debian signing key"
    fi
    read -r key_pub_count key_fingerprint_count key_fingerprint extra_field \
        <<< "${key_record}"
    [[ "${key_pub_count}" == "1" ]] &&
        [[ "${key_fingerprint_count}" == "1" ]] &&
        [[ "${key_fingerprint}" == "${ARTIFACTORY_KEY_FINGERPRINT}" ]] &&
        [[ -z "${extra_field}" ]] ||
        die "unexpected Artifactory Debian signing key fingerprint"

    if ! gpg \
        --batch \
        --no-options \
        --homedir "${artifactory_download_dir}/gnupg" \
        --no-default-keyring \
        --keyring "${artifactory_download_dir}/trustedkeys.gpg" \
        --quiet \
        --import "${ARTIFACTORY_PUBLIC_KEY}"; then
        die "failed to load the Artifactory Debian signing key"
    fi
    if ! signature_status="$(
        gpg \
            --batch \
            --no-options \
            --homedir "${artifactory_download_dir}/gnupg" \
            --no-default-keyring \
            --keyring "${artifactory_download_dir}/trustedkeys.gpg" \
            --status-fd=1 \
            --verify \
            "${artifactory_download_dir}/Release.gpg" \
            "${artifactory_download_dir}/Release" 2> /dev/null
    )"; then
        die "Artifactory Debian Release signature verification failed"
    fi
    signature_record="$(
        awk '
            $1 == "[GNUPG:]" && $2 == "VALIDSIG" {
                print $3, $NF
            }
        ' <<< "${signature_status}"
    )"
    [[ -n "${signature_record}" && "${signature_record}" != *$'\n'* ]] ||
        die "Artifactory Debian Release has an ambiguous signature"
    read -r signer_fingerprint signer_primary_fingerprint extra_field \
        <<< "${signature_record}"
    [[ "${signer_fingerprint}" == "${ARTIFACTORY_KEY_FINGERPRINT}" ]] &&
        [[ "${signer_primary_fingerprint}" == "${ARTIFACTORY_KEY_FINGERPRINT}" ]] &&
        [[ -z "${extra_field}" ]] ||
        die "Artifactory Debian Release was signed by an unexpected key"

    index_record="$(
        awk -v path="${package_index_path}" '
            $0 == "SHA256:" {
                in_sha256 = 1
                next
            }
            in_sha256 && $0 !~ /^ / {
                exit
            }
            in_sha256 && $3 == path {
                print $1, $2
            }
        ' "${artifactory_download_dir}/Release"
    )"
    [[ -n "${index_record}" && "${index_record}" != *$'\n'* ]] ||
        die "signed Artifactory Release has no unique package index"
    read -r expected_index_sha256 expected_index_size extra_field \
        <<< "${index_record}"
    [[ -z "${extra_field}" ]] ||
        die "signed Artifactory Release has malformed package index metadata"

    artifactory_curl "${auth_file}" \
        --output "${artifactory_download_dir}/Packages" \
        "${metadata_url}/${package_index_path}" || return 1
    verify_file \
        "${artifactory_download_dir}/Packages" \
        "${expected_index_sha256}" \
        "${expected_index_size}" ||
        die "Artifactory Packages index does not match the signed Release"

    package_record="$(
        awk \
            -v RS='' \
            -v expected_package="${PACKAGE_NAME}" \
            -v expected_version="${PACKAGE_VERSION}-${PACKAGE_REVISION}" \
            -v expected_architecture="${package_arch}" '
            {
                package = ""
                version = ""
                architecture = ""
                filename = ""
                size = ""
                sha256 = ""
                package_count = 0
                version_count = 0
                architecture_count = 0
                filename_count = 0
                size_count = 0
                sha256_count = 0
                line_count = split($0, lines, "\n")
                for (line_number = 1; line_number <= line_count; line_number++) {
                    if (lines[line_number] ~ /^Package: /) {
                        package = substr(lines[line_number], 10)
                        package_count++
                    } else if (lines[line_number] ~ /^Version: /) {
                        version = substr(lines[line_number], 10)
                        version_count++
                    } else if (lines[line_number] ~ /^Architecture: /) {
                        architecture = substr(lines[line_number], 15)
                        architecture_count++
                    } else if (lines[line_number] ~ /^Filename: /) {
                        filename = substr(lines[line_number], 11)
                        filename_count++
                    } else if (lines[line_number] ~ /^Size: /) {
                        size = substr(lines[line_number], 7)
                        size_count++
                    } else if (lines[line_number] ~ /^SHA256: /) {
                        sha256 = substr(lines[line_number], 9)
                        sha256_count++
                    }
                }
                if (package == expected_package &&
                    version == expected_version &&
                    architecture == expected_architecture &&
                    package_count == 1 &&
                    version_count == 1 &&
                    architecture_count == 1 &&
                    filename_count == 1 &&
                    size_count == 1 &&
                    sha256_count == 1) {
                    print filename, sha256, size
                }
            }
        ' "${artifactory_download_dir}/Packages"
    )"
    [[ -n "${package_record}" && "${package_record}" != *$'\n'* ]] ||
        die "Artifactory Packages index has no unique exact package"
    read -r \
        package_path \
        expected_package_sha256 \
        expected_package_size \
        extra_field \
        <<< "${package_record}"
    [[ -z "${extra_field}" ]] ||
        die "Artifactory package metadata is malformed"
    [[ "${package_path}" == "pool/${requested_package}" ]] ||
        die "Artifactory package path does not match the requested package"

    package_url="${repository_url}/${package_path}"
    artifactory_curl "${auth_file}" \
        --output "${artifactory_download_dir}/${requested_package}" \
        "${package_url}" || return 1
    verify_file \
        "${artifactory_download_dir}/${requested_package}" \
        "${expected_package_sha256}" \
        "${expected_package_size}" ||
        die "Artifactory package does not match its signed index"
    validate_package_metadata \
        "${artifactory_download_dir}/${requested_package}" \
        "${package_arch}" ||
        die "Artifactory package metadata does not match the request"
    install -m 0644 "${artifactory_download_dir}/${requested_package}" "${destination}"
    log "Downloaded ${requested_package} from Artifactory"
}

fetch_package() {
    local package_arch="$1"
    local requested_package="$2"
    local local_package="$3"
    local destination="${FETCH_DIR%/}/${requested_package}"

    mkdir -p "${FETCH_DIR}"
    if [[ -f "${local_package}" ]]; then
        if validate_package_metadata "${local_package}" "${package_arch}"; then
            install -m 0644 "${local_package}" "${destination}"
            log "Staged local ExecuTorch package: ${destination}"
            return 0
        fi
        warn "Ignoring local ExecuTorch package with mismatched metadata: ${local_package}"
    fi

    if download_from_artifactory "${package_arch}" "${requested_package}" "${destination}"; then
        return 0
    fi

    rm -f "${destination}"
    if [[ "${EXECUTORCH_REQUIRED}" == "1" ]]; then
        die "ExecuTorch ${PACKAGE_VERSION}-${PACKAGE_REVISION} is required but unavailable"
    fi
    log "ExecuTorch source: unavailable; continuing without ExecuTorch support"
}

need_cmd dpkg

if [[ $# -gt 0 ]]; then
    if [[ $# -eq 1 && "$1" == "--check-installed" ]]; then
        executorch_sdk_is_installed
        exit
    fi
    die "usage: install-executorch-deb.sh [--check-installed]"
fi

if [[ -n "${PACKAGE_ARCHITECTURE}" && -z "${FETCH_DIR}" ]]; then
    die "EXECUTORCH_DEB_ARCHITECTURE is only valid while fetching a package"
fi
package_arch="${PACKAGE_ARCHITECTURE:-$(dpkg --print-architecture)}"
package_filename="${PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_REVISION}-${package_arch}.deb"
local_package="${PACKAGE_DIR%/}/${package_filename}"

if [[ -n "${FETCH_DIR}" ]]; then
    [[ "${FETCH_DIR}" == /* && "${FETCH_DIR}" != "/" ]] ||
        die "EXECUTORCH_DEB_FETCH_DIR must be an absolute non-root path"
    case "/${FETCH_DIR#/}/" in
        */../* | */./*)
            die "EXECUTORCH_DEB_FETCH_DIR must not contain . or .. path components"
            ;;
    esac

    need_cmd awk
    need_cmd cat
    need_cmd curl
    need_cmd dpkg-deb
    need_cmd gpg
    need_cmd install
    need_cmd mktemp
    need_cmd sha256sum
    need_cmd stat

    case "${package_arch}" in
        amd64 | arm64) ;;
        *) die "unsupported ExecuTorch package architecture: ${package_arch}" ;;
    esac

    if [[ -z "${ARTIFACTORY_USERNAME}" ]]; then
        ARTIFACTORY_USERNAME="$(
            read_secret_file "${ARTIFACTORY_USERNAME_FILE}" || true
        )"
    fi
    if [[ -z "${ARTIFACTORY_PASSWORD}" ]]; then
        ARTIFACTORY_PASSWORD="$(
            read_secret_file "${ARTIFACTORY_PASSWORD_FILE}" || true
        )"
    fi
    fetch_package "${package_arch}" "${package_filename}" "${local_package}"
    exit 0
fi

if [[ -f "${local_package}" ]]; then
    if install_local_package "${local_package}"; then
        exit 0
    fi
else
    log "Local ExecuTorch package not found: ${local_package}"
fi

if [[ "${EXECUTORCH_REQUIRED}" == "1" ]]; then
    die "ExecuTorch ${PACKAGE_VERSION}-${PACKAGE_REVISION} is required but unavailable"
fi

log "ExecuTorch source: unavailable; continuing without ExecuTorch support"
