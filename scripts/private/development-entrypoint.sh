#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

: "${USERNAME:=dev}"
: "${HOST_UID:=}"
: "${HOST_GID:=}"
OPK_PROJECT_ROOT="$(cd "${OPK_PROJECT_ROOT:-.}" && pwd -P)"
: "${CCACHE_DIR:=${OPK_PROJECT_ROOT}/.cache/ccache}"
export OPK_PROJECT_ROOT CCACHE_DIR

ENTRYPOINT_READY_FILE="/tmp/opk-development-entrypoint-ready"
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
    local artifacts_root="/opt/opk-app"
    local video_artifacts="${artifacts_root}/data/videos"

    [[ -d "${artifacts_root}" ]] || return 0
    [[ -w "${OPK_PROJECT_ROOT}" ]] || return 0

    if [[ -d /opt/opk-ccache ]]; then
        run_as_development_user mkdir -p "${CCACHE_DIR}"
        run_as_development_user cp -a --no-clobber /opt/opk-ccache/. "${CCACHE_DIR}/"
    fi

    if [[ -d "${artifacts_root}/config/models" ]]; then
        run_as_development_user mkdir -p "${OPK_PROJECT_ROOT}/config/models"
        run_as_development_user cp -R --no-clobber \
            "${artifacts_root}/config/models/." "${OPK_PROJECT_ROOT}/config/models/"
    fi

    if [[ -d "${video_artifacts}" ]]; then
        run_as_development_user mkdir -p "${OPK_PROJECT_ROOT}/data/videos"
        run_as_development_user cp -a --no-clobber \
            "${video_artifacts}/." "${OPK_PROJECT_ROOT}/data/videos/"
        if [[ -f "${video_artifacts}/SHA256SUMS" ]]; then
            if ! (cd "${OPK_PROJECT_ROOT}/data/videos" && sha256sum --check --strict --quiet "${video_artifacts}/SHA256SUMS"); then
                echo "ERROR: existing demo videos failed checksum validation." >&2
                echo "Remove them and retry the quick start:" >&2
                echo "  rm -rf data/videos && ./scripts/quick_start.sh" >&2
                exit 1
            fi
        fi
    fi

    if [[ -d "${artifacts_root}/development/build/meson-out" ]]; then
        run_as_development_user mkdir -p "${OPK_PROJECT_ROOT}/development/build/meson-out"
        run_as_development_user cp -a --no-clobber \
            "${artifacts_root}/development/build/meson-out/." \
            "${OPK_PROJECT_ROOT}/development/build/meson-out/"
    fi

    if [[ ! -e "${OPK_PROJECT_ROOT}/tools/opk-menu" && -f "${artifacts_root}/tools/opk-menu" ]]; then
        run_as_development_user mkdir -p "${OPK_PROJECT_ROOT}/tools"
        run_as_development_user cp -a \
            "${artifacts_root}/tools/opk-menu" "${OPK_PROJECT_ROOT}/tools/opk-menu"
    fi
}

if [[ "${1:-}" == "--seed-artifacts" ]]; then
    if [[ -d /opt/opk-app && ! -w "${OPK_PROJECT_ROOT}" ]]; then
        echo "ERROR: cannot seed development artifacts into ${OPK_PROJECT_ROOT}" >&2
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
mkdir -p "${OPK_PROJECT_ROOT}" "${CCACHE_DIR}" "${OPK_PROJECT_ROOT}/development/build"
chown "${HOST_UID}:${HOST_GID}" "${OPK_PROJECT_ROOT}"
chown -R "${HOST_UID}:${HOST_GID}" "${CCACHE_DIR}" "${OPK_PROJECT_ROOT}/development/build"
chown -R "${HOST_UID}:${HOST_GID}" "/home/${USERNAME}" || true
chown "${HOST_UID}:${HOST_GID}" /tmp/opkcomm || true

seed_development_artifacts

# Drop privileges
touch "${ENTRYPOINT_READY_FILE}"
exec gosu "${HOST_UID}:${HOST_GID}" "$@"
