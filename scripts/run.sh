#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Runs PEK pipelines through pek-menu inside the quick-start container.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/run.sh [pek-menu-args...]
  ./scripts/run.sh --menu
  ./scripts/run.sh -h|--help

Runs /work/tools/pek-menu inside the PEK quick-start container.

Defaults:
  ./scripts/run.sh              Runs the first sample pipeline: 01-full-onnx
  ./scripts/run.sh --menu       Opens the interactive pek-menu
  ./scripts/run.sh -l           Runs the last selected pipeline
  ./scripts/run.sh <pipeline>   Runs a pipeline by ID or JSON path

If this command is run inside the PEK container, it calls pek-menu directly.
If it is run on the host, it starts the matching quick-start container if
needed, then calls pek-menu through docker exec.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DETECT_SCRIPT="${REPO_ROOT}/scripts/quick-start/detect-environment.sh"
START_CONTAINER_SCRIPT="${REPO_ROOT}/scripts/quick-start/start-container.sh"

PEK_MENU_ARGS=("$@")
if [[ $# -eq 0 ]]; then
    PEK_MENU_ARGS=(01-full-onnx)
elif [[ "${1:-}" == "--menu" ]]; then
    shift
    if [[ $# -gt 0 ]]; then
        echo "Error: --menu does not accept extra arguments." >&2
        usage >&2
        exit 2
    fi
    PEK_MENU_ARGS=()
fi

if ! detect_output="$("${DETECT_SCRIPT}" --shell)"; then
    eval "$detect_output"
    echo "Error: unsupported quick-start platform: ${PEK_PLATFORM_NAME:-unknown}" >&2
    if [[ -n "${PEK_UNSUPPORTED_REASON:-}" ]]; then
        echo "Reason: ${PEK_UNSUPPORTED_REASON}" >&2
    fi
    exit 1
fi
eval "$detect_output"

if [[ "${PEK_IN_CONTAINER}" == "true" ]]; then
    if [[ ! -x /work/tools/pek-menu ]]; then
        echo "Error: /work/tools/pek-menu is missing or not executable." >&2
        echo "Run ./scripts/build.sh first." >&2
        exit 1
    fi

    cd /work
    exec /work/tools/pek-menu "${PEK_MENU_ARGS[@]}"
fi

cd "${REPO_ROOT}"

if ! docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Container is not running: ${PEK_CONTAINER_NAME}"
    echo "Starting it now..."
    "${START_CONTAINER_SCRIPT}"
fi

if ! docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Error: container did not start: ${PEK_CONTAINER_NAME}" >&2
    exit 1
fi

if ! docker exec -u devgoblin "${PEK_CONTAINER_NAME}" bash -lc 'test -w /work' > /dev/null 2>&1; then
    echo "Container /work is not writable as devgoblin."
    echo "Recreating it with the host UID/GID mapping..."
    "${START_CONTAINER_SCRIPT}" --recreate
fi

if ! docker exec -u devgoblin "${PEK_CONTAINER_NAME}" bash -lc 'test -x /work/tools/pek-menu' > /dev/null 2>&1; then
    echo "Error: /work/tools/pek-menu is missing or not executable in ${PEK_CONTAINER_NAME}." >&2
    echo "Run ./scripts/build.sh first." >&2
    exit 1
fi

DOCKER_EXEC_ARGS=(docker exec)
if [[ -t 0 && -t 1 ]]; then
    DOCKER_EXEC_ARGS+=(-it)
fi
DOCKER_EXEC_ARGS+=(-u devgoblin)
if [ -f "${REPO_ROOT}/devices.env" ]; then
    DOCKER_EXEC_ARGS+=(--env-file "${REPO_ROOT}/devices.env")
fi
DOCKER_EXEC_ARGS+=("${PEK_CONTAINER_NAME}")

exec "${DOCKER_EXEC_ARGS[@]}" bash -lc 'cd /work && exec /work/tools/pek-menu "$@"' bash "${PEK_MENU_ARGS[@]}"
