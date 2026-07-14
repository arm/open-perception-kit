#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  run-console-enter [-h]

Enters the console based development environment.

Notes:
  Requires the PEK development container to be running.
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

DEV_ENV_FILE="devices.env"
DC_KIND="devcont"
CONTAINER_NAME="${PEK_DEV_CONTAINER_NAME:-perception-experience-kit}"

cd "${REPO_ROOT}"

# Check if container is running
if ! docker inspect -f '{{.State.Running}}' "${CONTAINER_NAME}" > /dev/null 2>&1; then
    echo "Error: container '${CONTAINER_NAME}' is not running." >&2
    echo "Please start it first by running: ./scripts/private/run-console" >&2
    exit 1
fi

./scripts/private/dev-init.sh pek-dev "$DC_KIND" "$DEV_ENV_FILE"

DOCKER_EXEC_ENV_FILE_ARGS=()
if [ -f "${REPO_ROOT}/${DEV_ENV_FILE}" ]; then
    DOCKER_EXEC_ENV_FILE_ARGS=(--env-file "${REPO_ROOT}/${DEV_ENV_FILE}")
fi
docker exec -it -u dev "${DOCKER_EXEC_ENV_FILE_ARGS[@]}" -e TERM="$TERM" "${CONTAINER_NAME}" zsh
