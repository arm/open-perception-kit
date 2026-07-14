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

DEV_ENV_FILE="devices.env"
DC_KIND="devcont"
CONTAINER_NAME="${PEK_DEV_CONTAINER_NAME:-perception-experience-kit}"

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT:-49000}"
export PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT:-49050}"

cd "${REPO_ROOT}"

# Compose files used for lifecycle commands
COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".video.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".audio.yaml
    -f .devcontainer/docker-compose."${DC_KIND}".shared_memory.yaml
)

COMPOSE_ENV_ARGS=()
if [[ -f "${REPO_ROOT}/.env" ]]; then
    COMPOSE_ENV_ARGS=(--env-file "${REPO_ROOT}/.env")
fi

is_running() {
    docker inspect -f '{{.State.Running}}' "${CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'
}

detect_webrtc_host_ip() {
    if [[ -z "${WEBRTC_HOST_IP:-}" ]]; then
        WEBRTC_HOST_IP="$(bash "${SCRIPT_DIR}/detect-webrtc-host-ip.sh")"
    fi
    export WEBRTC_HOST_IP
}

do_up() {
    detect_webrtc_host_ip
    ./scripts/private/dev-init.sh pek-dev "$DC_KIND" "$DEV_ENV_FILE"

    echo "Using WebRTC host IP: ${WEBRTC_HOST_IP}"
    echo "Using WebRTC TURN relay ports: ${PEK_WEBRTC_TURN_MIN_PORT}-${PEK_WEBRTC_TURN_MAX_PORT}"

    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" WEBRTC_HOST_IP="${WEBRTC_HOST_IP}" \
        PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT}" \
        PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT}" \
        docker compose "${COMPOSE_ENV_ARGS[@]}" "${COMPOSE_FILES[@]}" up -d --build coturn pek-dev
}

do_down() {
    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" WEBRTC_HOST_IP="${WEBRTC_HOST_IP:-}" \
        PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT}" \
        PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT}" \
        docker compose "${COMPOSE_ENV_ARGS[@]}" "${COMPOSE_FILES[@]}" down
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
