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
# Enters the quick-start development container, starting it first if needed.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/enter_cli.sh [-h|--help]

Starts the detected OPK base development container if it is not already running,
then enters it with the dev user's interactive login shell.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ "${1:-}" != "" ]]; then
    echo "Error: unknown argument '${1}'" >&2
    echo >&2
    usage >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DETECT_SCRIPT="${REPO_ROOT}/scripts/quick-start/detect-environment.sh"
START_CONTAINER_SCRIPT="${REPO_ROOT}/scripts/quick-start/start-container.sh"

if ! detect_output="$("${DETECT_SCRIPT}" --shell)"; then
    eval "$detect_output"
    echo "Error: unsupported quick-start platform: ${OPK_PLATFORM_NAME:-unknown}" >&2
    if [[ -n "${OPK_UNSUPPORTED_REASON:-}" ]]; then
        echo "Reason: ${OPK_UNSUPPORTED_REASON}" >&2
    fi
    exit 1
fi
eval "$detect_output"

cd "${REPO_ROOT}"

if ! docker inspect -f '{{.State.Running}}' "${OPK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Container is not running: ${OPK_CONTAINER_NAME}"
    echo "Starting it now..."
    "${START_CONTAINER_SCRIPT}"
fi

if docker inspect -f '{{.State.Running}}' "${OPK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$' &&
    ! docker exec -u dev "${OPK_CONTAINER_NAME}" test -w /work > /dev/null 2>&1; then
    echo "Container /work is not writable as dev."
    echo "Recreating it with the host UID/GID mapping..."
    "${START_CONTAINER_SCRIPT}" --recreate
fi

if ! docker inspect -f '{{.State.Running}}' "${OPK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Error: container did not start: ${OPK_CONTAINER_NAME}" >&2
    exit 1
fi

if ! dev_passwd_entry="$(docker exec "${OPK_CONTAINER_NAME}" getent passwd dev)"; then
    echo "Error: could not determine the dev user's login shell." >&2
    exit 1
fi
DEV_LOGIN_SHELL="${dev_passwd_entry##*:}"
if [[ "${DEV_LOGIN_SHELL}" != /* ]]; then
    echo "Error: invalid login shell for dev: ${DEV_LOGIN_SHELL}" >&2
    exit 1
fi

DOCKER_EXEC_ARGS=(docker exec -it -u dev)
if [ -f "${REPO_ROOT}/devices.env" ]; then
    DOCKER_EXEC_ARGS+=(--env-file "${REPO_ROOT}/devices.env")
fi
DOCKER_EXEC_ARGS+=(-e TERM="${TERM:-xterm-256color}" "${OPK_CONTAINER_NAME}" "${DEV_LOGIN_SHELL}" -l)
exec "${DOCKER_EXEC_ARGS[@]}"
