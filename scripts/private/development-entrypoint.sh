#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

: "${USERNAME:=dev}"
: "${HOST_UID:=}"
: "${HOST_GID:=}"

seed_development_artifacts() {
    local artifacts_root="/opt/pek-app"

    [[ -d "${artifacts_root}" ]] || return 0
    [[ -w /work ]] || return 0

    mkdir -p /work/data/videos /work/development/build/meson-out /work/tools

    if [[ -d "${artifacts_root}/data/videos" ]]; then
        cp -a --no-clobber "${artifacts_root}/data/videos/." /work/data/videos/
    fi

    if [[ -d "${artifacts_root}/development/build/meson-out" ]]; then
        cp -a --no-clobber \
            "${artifacts_root}/development/build/meson-out/." \
            /work/development/build/meson-out/
    fi

    if [[ ! -e /work/tools/pek-menu && -f "${artifacts_root}/tools/pek-menu" ]]; then
        cp -a "${artifacts_root}/tools/pek-menu" /work/tools/pek-menu
    fi
}

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
