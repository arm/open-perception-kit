#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Enters the quick-start development container, starting it first if needed.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/enter_cli.sh [-h|--help]

Starts the detected PEK base development container if it is not already running,
then enters it with an interactive bash shell.
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
    echo "Error: unsupported quick-start platform: ${PEK_PLATFORM_NAME:-unknown}" >&2
    if [[ -n "${PEK_UNSUPPORTED_REASON:-}" ]]; then
        echo "Reason: ${PEK_UNSUPPORTED_REASON}" >&2
    fi
    exit 1
fi
eval "$detect_output"

cd "${REPO_ROOT}"

if ! docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Container is not running: ${PEK_CONTAINER_NAME}"
    echo "Starting it now..."
    "${START_CONTAINER_SCRIPT}"
fi

if docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$' &&
    ! docker exec -u dev "${PEK_CONTAINER_NAME}" bash -lc 'test -w /work' > /dev/null 2>&1; then
    echo "Container /work is not writable as dev."
    echo "Recreating it with the host UID/GID mapping..."
    "${START_CONTAINER_SCRIPT}" --recreate
fi

if ! docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Error: container did not start: ${PEK_CONTAINER_NAME}" >&2
    exit 1
fi

DOCKER_EXEC_ARGS=(docker exec -it -u dev)
if [ -f "${REPO_ROOT}/devices.env" ]; then
    DOCKER_EXEC_ARGS+=(--env-file "${REPO_ROOT}/devices.env")
fi
DOCKER_EXEC_ARGS+=(-e TERM="${TERM:-xterm-256color}" "${PEK_CONTAINER_NAME}" bash)
exec "${DOCKER_EXEC_ARGS[@]}"
