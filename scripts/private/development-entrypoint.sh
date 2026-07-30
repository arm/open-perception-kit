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

    [[ -d "${artifacts_root}" ]] || return 0
    [[ -w /work ]] || return 0

    mkdir -p \
        /work/config/models \
        /work/data/videos \
        /work/development/build/meson-out \
        /work/tools

    if [[ -d "${artifacts_root}/config/models" ]]; then
        cp -a --no-clobber "${artifacts_root}/config/models/." /work/config/models/
        python3 - "${artifacts_root}/config/models" /work/config/models << 'PY'
import json
import shutil
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
for descriptor in source.rglob("*.json"):
    model = json.loads(descriptor.read_text())
    if "hfDownload" not in model:
        continue
    model_file = Path(model["modelFile"])
    target = destination / descriptor.parent.relative_to(source) / model_file
    source_artifact = descriptor.parent / model_file
    if not source_artifact.is_file():
        target.unlink(missing_ok=True)
        continue
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_artifact, target)
PY
    fi

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
