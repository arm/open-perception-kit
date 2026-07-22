#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  run-console [up|down] [-h|--help]

Starts or stops the development environment in a Docker container.

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

DC_KIND="devcont"
CONTAINER_NAME="${PEK_DEV_CONTAINER_NAME:-perception-experience-kit}"

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"

cd "${REPO_ROOT}"

# Compose files used for lifecycle commands
COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".video.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".audio.yaml
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
    export PEK_DEV_CONTAINER_NAME="${CONTAINER_NAME}"

    ./.devcontainer/platform_init.sh \
        pek-dev "${PEK_PICAMERA:-disabled}" "${PEK_WEBRTC_TURN}"
    bash ./scripts/private/build-dev-base.sh

    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" \
        docker compose "${COMPOSE_ENV_ARGS[@]}" "${COMPOSE_FILES[@]}" \
        up -d --build --remove-orphans pek-dev
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
