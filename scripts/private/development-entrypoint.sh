#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

: "${USERNAME:=dev}"
: "${HOST_UID:=}"
: "${HOST_GID:=}"

# If the user of the container has a zsh config then use that configuration inside the container, otherwise keep using the default one.
if [[ -f "/home/${USERNAME}/configs/zshrc" ]]; then
    ln -sfn "/home/${USERNAME}/configs/zshrc" "/home/${USERNAME}/.zshrc"
fi

seed_development_artifacts() {
    local artifacts_root="/opt/pek-app"
    local video_artifacts="${artifacts_root}/data/videos"

    [[ -d "${artifacts_root}" ]] || return 0
    [[ -w /work ]] || return 0

    if [[ -d "${artifacts_root}/config/models" ]]; then
        mkdir -p /work/config/models
        cp -R --no-clobber "${artifacts_root}/config/models/." /work/config/models/
    fi

    if [[ -d "${video_artifacts}" ]]; then
        mkdir -p /work/data/videos
        cp -a --no-clobber "${video_artifacts}/." /work/data/videos/
        if [[ -f "${video_artifacts}/SHA256SUMS" ]]; then
            if ! (cd /work/data/videos && sha256sum --check --strict --quiet "${video_artifacts}/SHA256SUMS"); then
                echo "ERROR: existing demo videos failed checksum validation." >&2
                echo "Remove them and retry the quick start:" >&2
                echo "  rm -rf data/videos && ./scripts/quick_start.sh" >&2
                exit 1
            fi
        fi
    fi

    if [[ -d "${artifacts_root}/development/build/meson-out" ]]; then
        mkdir -p /work/development/build/meson-out
        cp -a --no-clobber \
            "${artifacts_root}/development/build/meson-out/." \
            /work/development/build/meson-out/
    fi

    if [[ ! -e /work/tools/pek-menu && -f "${artifacts_root}/tools/pek-menu" ]]; then
        mkdir -p /work/tools
        cp -a "${artifacts_root}/tools/pek-menu" /work/tools/pek-menu
    fi
}

if [[ "${1:-}" == "--seed-artifacts" ]]; then
    if [[ -d /opt/pek-app && ! -w /work ]]; then
        echo "ERROR: cannot seed development artifacts into /work" >&2
        exit 1
    fi
    seed_development_artifacts
    exit 0
fi

# If no remap requested, just run as current user
if [[ -z "${HOST_UID}" || -z "${HOST_GID}" ]]; then
    seed_development_artifacts
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

seed_development_artifacts

# Fix home ownership (keep it cheap)
chown -R "${HOST_UID}:${HOST_GID}" "/home/${USERNAME}" || true
mkdir -p /work
chown -R "${HOST_UID}:${HOST_GID}" /work || true
chown -R "${HOST_UID}:${HOST_GID}" /tmp/pekcomm || true

# Drop privileges
exec gosu "${HOST_UID}:${HOST_GID}" "$@"
