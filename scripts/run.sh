#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################
# Runs OPK pipelines through opk-menu inside the quick-start container.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/run.sh [opk-menu-args...]
  ./scripts/run.sh --menu
  ./scripts/run.sh -h|--help

Runs /work/tools/opk-menu inside the OPK quick-start container.

Defaults:
  ./scripts/run.sh              Runs the bundled YOLO26 sample: yolo26n-320
  ./scripts/run.sh --menu       Opens the interactive opk-menu
  ./scripts/run.sh -l           Runs the last selected pipeline
  ./scripts/run.sh <pipeline>   Runs a pipeline by ID or JSON path

If this command is run inside the OPK container, it calls opk-menu directly.
If it is run on the host, it starts the matching quick-start container if
needed, then calls opk-menu through docker exec.
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

OPK_MENU_ARGS=("$@")
if [[ $# -eq 0 ]]; then
    OPK_MENU_ARGS=(yolo26n-320)
elif [[ "${1:-}" == "--menu" ]]; then
    shift
    if [[ $# -gt 0 ]]; then
        echo "Error: --menu does not accept extra arguments." >&2
        usage >&2
        exit 2
    fi
    OPK_MENU_ARGS=()
fi

detect_status=0
detect_output="$("${DETECT_SCRIPT}" --shell)" || detect_status=$?
eval "$detect_output"

if [[ "${OPK_IN_CONTAINER}" == "true" ]]; then
    if [[ ! -x /work/tools/opk-menu ]]; then
        echo "Error: /work/tools/opk-menu is missing or not executable." >&2
        echo "Run ./scripts/build.sh first." >&2
        exit 1
    fi

    cd /work
    exec /work/tools/opk-menu "${OPK_MENU_ARGS[@]}"
fi

if ((detect_status != 0)); then
    echo "Error: unsupported quick-start platform: ${OPK_PLATFORM_NAME:-unknown}" >&2
    if [[ -n "${OPK_UNSUPPORTED_REASON:-}" ]]; then
        echo "Reason: ${OPK_UNSUPPORTED_REASON}" >&2
    fi
    exit 1
fi

cd "${REPO_ROOT}"

if ! docker inspect -f '{{.State.Running}}' "${OPK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Container is not running: ${OPK_CONTAINER_NAME}"
    echo "Starting it now..."
    "${START_CONTAINER_SCRIPT}"
fi

if ! docker inspect -f '{{.State.Running}}' "${OPK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Error: container did not start: ${OPK_CONTAINER_NAME}" >&2
    exit 1
fi

if ! docker exec -u dev "${OPK_CONTAINER_NAME}" bash -lc 'test -w /work' > /dev/null 2>&1; then
    echo "Container /work is not writable as dev."
    echo "Recreating it with the host UID/GID mapping..."
    "${START_CONTAINER_SCRIPT}" --recreate
fi

if ! docker exec -u dev "${OPK_CONTAINER_NAME}" bash -lc 'test -x /work/tools/opk-menu' > /dev/null 2>&1; then
    echo "Error: /work/tools/opk-menu is missing or not executable in ${OPK_CONTAINER_NAME}." >&2
    echo "Run ./scripts/build.sh first." >&2
    exit 1
fi

DOCKER_EXEC_ARGS=(docker exec)
if [[ -t 0 && -t 1 ]]; then
    DOCKER_EXEC_ARGS+=(-it)
fi
DOCKER_EXEC_ARGS+=(-u dev)
if [ -f "${REPO_ROOT}/devices.env" ]; then
    DOCKER_EXEC_ARGS+=(--env-file "${REPO_ROOT}/devices.env")
fi
DOCKER_EXEC_ARGS+=("${OPK_CONTAINER_NAME}")

exec "${DOCKER_EXEC_ARGS[@]}" bash -lc 'cd /work && exec /work/tools/opk-menu "$@"' bash "${OPK_MENU_ARGS[@]}"
