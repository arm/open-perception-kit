#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  run-console [up|down] [-h|--help]

Starts or stops the rich console development environment in Docker.

Commands:
  up        Build (if needed) and start the stack
  down      Stop and remove the stack
  (none)    Toggle: if running -> down, else -> up

Options:
  -h, --help   Show this help

Examples:
  run-console up
  run-console down
  run-console
EOF
}

# Ensure we run from repo root even if script is called elsewhere
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DC_KIND="rich"
CONTAINER_NAME="${PEK_RICH_CONTAINER_NAME:-pek-dev-rich}"

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"
export HOST_UID HOST_GID
export HF_TOKEN="${HF_TOKEN-}"
export HF_DOWNLOAD_CACHEBUST="${HF_DOWNLOAD_CACHEBUST:-$(date +%s)-$$}"

cd "${REPO_ROOT}"

# Compose files used for lifecycle commands
COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".yaml
    -f .devcontainer/docker-compose."${DC_KIND}".video.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".audio.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".npu.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".shared_memory.yaml
)

PEK_WEBRTC_TURN="$(bash scripts/private/select-webrtc-turn-mode.sh)"
if [[ "${PEK_WEBRTC_TURN}" == enabled ]]; then
    COMPOSE_FILES+=(-f .devcontainer/docker-compose."${DC_KIND}".turn.yaml)
fi

COMPOSE_ENV_ARGS=()
if [[ -f "${REPO_ROOT}/.env" ]]; then
    COMPOSE_ENV_ARGS=(--env-file "${REPO_ROOT}/.env")
fi

is_running() {
    docker inspect -f '{{.State.Running}}' "${CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'
}

do_up() {
    export PEK_RICH_CONTAINER_NAME="${CONTAINER_NAME}"

    if [[ -z "${PEK_RICH_CONFIGS_MOUNT:-}" && -d "${HOME}/configs" ]]; then
        export PEK_RICH_CONFIGS_MOUNT="${HOME}/configs"
    fi
    if [[ -z "${PEK_RICH_WORK_TREE_MOUNT:-}" && -d "${REPO_ROOT}/../pek-work-tree" ]]; then
        export PEK_RICH_WORK_TREE_MOUNT="${REPO_ROOT}/../pek-work-tree"
    fi

    ./.devcontainer/platform_init.sh \
        pek-dev-rich "${PEK_PICAMERA:-disabled}" "${PEK_WEBRTC_TURN}" "${DC_KIND}"
    bash ./scripts/private/build-dev-base.sh

    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" \
        docker compose "${COMPOSE_ENV_ARGS[@]}" "${COMPOSE_FILES[@]}" \
        up -d --build --force-recreate --remove-orphans pek-dev-rich
}

do_down() {
    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" \
        docker compose "${COMPOSE_ENV_ARGS[@]}" "${COMPOSE_FILES[@]}" down --remove-orphans
}

cmd="${1:-}"

case "${cmd}" in
    -h | --help)
        usage
        exit 0
        ;;
    up)
        do_up
        ;;
    down)
        do_down
        ;;
    "")
        if is_running; then
            do_down
        else
            do_up
        fi
        ;;
    *)
        echo "Error: unknown argument '${cmd}'" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac
