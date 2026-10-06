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

usage() {
    cat << 'EOF'
Usage:
  run-console-enter [-h]

Enters the console based development environment.

Notes:
  Requires the rich OPK development container to be running.
  If it isn't running, start it with: ./scripts/private/run-console
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
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DEV_ENV_FILE="devices.env" # agent-static-analysis: allow-generated-path
DC_KIND="rich"
CONTAINER_NAME="${OPK_RICH_CONTAINER_NAME:-opk-dev-rich}"

cd "${REPO_ROOT}"

# Check if container is running
if ! docker inspect -f '{{.State.Running}}' "${CONTAINER_NAME}" > /dev/null 2>&1; then
    echo "Error: container '${CONTAINER_NAME}' is not running." >&2
    echo "Please start it first by running: ./scripts/private/run-console" >&2
    exit 1
fi

./scripts/private/dev-init.sh opk-dev-rich "$DC_KIND" "$DEV_ENV_FILE"

DOCKER_EXEC_ENV_FILE_ARGS=()
if [ -f "${REPO_ROOT}/${DEV_ENV_FILE}" ]; then
    DOCKER_EXEC_ENV_FILE_ARGS=(--env-file "${REPO_ROOT}/${DEV_ENV_FILE}")
fi
docker exec -it -u dev "${DOCKER_EXEC_ENV_FILE_ARGS[@]}" -e TERM="$TERM" "${CONTAINER_NAME}" zsh
