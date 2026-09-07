#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

: "${USERNAME:=dev}"
: "${HOST_UID:=}"
: "${HOST_GID:=}"
PEK_PROJECT_ROOT="$(cd "${PEK_PROJECT_ROOT:-.}" && pwd -P)"
: "${CCACHE_DIR:=${PEK_PROJECT_ROOT}/.cache/ccache}"
export PEK_PROJECT_ROOT CCACHE_DIR

ENTRYPOINT_READY_FILE="/tmp/pek-development-entrypoint-ready"
rm -f "${ENTRYPOINT_READY_FILE}"

run_as_development_user() {
    if [[ -n "${HOST_UID}" && -n "${HOST_GID}" && "$(id -u)" -eq 0 ]]; then
        gosu "${HOST_UID}:${HOST_GID}" "$@"
    else
        "$@"
    fi
}

# If the user of the container has a zsh config then use that configuration inside the container, otherwise keep using the default one.
if [[ -f "/home/${USERNAME}/configs/zshrc" ]]; then
    ln -sfn "/home/${USERNAME}/configs/zshrc" "/home/${USERNAME}/.zshrc"
fi

seed_development_artifacts() {
    local artifacts_root="/opt/pek-app"
    local video_artifacts="${artifacts_root}/data/videos"

    [[ -d "${artifacts_root}" ]] || return 0
    [[ -w "${PEK_PROJECT_ROOT}" ]] || return 0

    if [[ -d /opt/pek-ccache ]]; then
        run_as_development_user mkdir -p "${CCACHE_DIR}"
        run_as_development_user cp -a --no-clobber /opt/pek-ccache/. "${CCACHE_DIR}/"
    fi

    if [[ -d "${artifacts_root}/config/models" ]]; then
        run_as_development_user mkdir -p "${PEK_PROJECT_ROOT}/config/models"
        run_as_development_user cp -R --no-clobber \
            "${artifacts_root}/config/models/." "${PEK_PROJECT_ROOT}/config/models/"
    fi

    if [[ -d "${video_artifacts}" ]]; then
        run_as_development_user mkdir -p "${PEK_PROJECT_ROOT}/data/videos"
        run_as_development_user cp -a --no-clobber \
            "${video_artifacts}/." "${PEK_PROJECT_ROOT}/data/videos/"
        if [[ -f "${video_artifacts}/SHA256SUMS" ]]; then
            if ! (cd "${PEK_PROJECT_ROOT}/data/videos" && sha256sum --check --strict --quiet "${video_artifacts}/SHA256SUMS"); then
                echo "ERROR: existing demo videos failed checksum validation." >&2
                echo "Remove them and retry the quick start:" >&2
                echo "  rm -rf data/videos && ./scripts/quick_start.sh" >&2
                exit 1
            fi
        fi
    fi

    if [[ -d "${artifacts_root}/development/build/meson-out" ]]; then
        run_as_development_user mkdir -p "${PEK_PROJECT_ROOT}/development/build/meson-out"
        run_as_development_user cp -a --no-clobber \
            "${artifacts_root}/development/build/meson-out/." \
            "${PEK_PROJECT_ROOT}/development/build/meson-out/"
    fi

    if [[ ! -e "${PEK_PROJECT_ROOT}/tools/pek-menu" && -f "${artifacts_root}/tools/pek-menu" ]]; then
        run_as_development_user mkdir -p "${PEK_PROJECT_ROOT}/tools"
        run_as_development_user cp -a \
            "${artifacts_root}/tools/pek-menu" "${PEK_PROJECT_ROOT}/tools/pek-menu"
    fi
}

if [[ "${1:-}" == "--seed-artifacts" ]]; then
    if [[ -d /opt/pek-app && ! -w "${PEK_PROJECT_ROOT}" ]]; then
        echo "ERROR: cannot seed development artifacts into ${PEK_PROJECT_ROOT}" >&2
        exit 1
    fi
    seed_development_artifacts
    touch "${ENTRYPOINT_READY_FILE}"
    exit 0
fi

# If no remap requested, just run as current user
if [[ -z "${HOST_UID}" || -z "${HOST_GID}" ]]; then
    seed_development_artifacts
    touch "${ENTRYPOINT_READY_FILE}"
    exec "$@"
fi

# Need root to remap ids
if [[ "$(id -u)" -ne 0 ]]; then
    echo "ERROR: need to start container as root to remap UID/GID (set --user root)" >&2
    exit 1
fi

# Ensure group exists with HOST_GID
if ! getent group "${HOST_GID}" > /dev/null; then
    groupadd -g "${HOST_GID}" "${USERNAME}" 2> /dev/null || groupadd -g "${HOST_GID}" hostgroup
fi

# Ensure user exists
if ! id -u "${USERNAME}" > /dev/null 2>&1; then
    useradd -m -s /bin/bash -u "${HOST_UID}" -g "${HOST_GID}" "${USERNAME}"
fi

# Update user/group ids
usermod -u "${HOST_UID}" "${USERNAME}" || true
groupmod -g "${HOST_GID}" "$(id -gn "${USERNAME}")" || true
usermod -g "${HOST_GID}" "${USERNAME}" || true

# Keep recursive ownership changes inside container-owned state. The checkout
# bind already belongs to the host user and can be expensive to traverse.
mkdir -p "${PEK_PROJECT_ROOT}" "${CCACHE_DIR}" "${PEK_PROJECT_ROOT}/development/build"
chown "${HOST_UID}:${HOST_GID}" "${PEK_PROJECT_ROOT}"
chown -R "${HOST_UID}:${HOST_GID}" "${CCACHE_DIR}" "${PEK_PROJECT_ROOT}/development/build"
chown -R "${HOST_UID}:${HOST_GID}" "/home/${USERNAME}" || true
chown "${HOST_UID}:${HOST_GID}" /tmp/pekcomm || true

seed_development_artifacts

# Drop privileges
touch "${ENTRYPOINT_READY_FILE}"
exec gosu "${HOST_UID}:${HOST_GID}" "$@"
