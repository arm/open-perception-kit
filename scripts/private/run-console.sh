#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  run-console [up|down] [-h|--help]

Starts or stops the console base development environment in Docker container,
similar to VS Code devcontainer, but provides richer environment.

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
DC_RICH="rich"

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT:-49000}"
export PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT:-49050}"

cd "${REPO_ROOT}"

# Compose files used for lifecycle commands
COMPOSE_FILES=(
    -f .devcontainer/docker-compose."${DC_RICH}".yaml
    -f .devcontainer/docker-compose."${DC_RICH}".video.yaml
    -f .devcontainer/docker-compose."${DC_RICH}".audio.yaml
    -f .devcontainer/docker-compose."${DC_RICH}".npu.yaml
    -f .devcontainer/docker-compose."${DC_RICH}".shared_memory.yaml
)

# Detect whether any service from this project is currently running
is_running() {
    # `docker compose ps -q` returns container IDs for services in the project.
    # We count how many are in "running" state.
    local ids
    ids="$(docker compose "${COMPOSE_FILES[@]}" ps -q || true)"
    [[ -z "${ids}" ]] && return 1
    docker inspect -f '{{.State.Running}}' ${ids} 2> /dev/null | grep -q '^true$'
}

detect_webrtc_host_ip() {
    if [[ -z "${WEBRTC_HOST_IP:-}" ]]; then
        WEBRTC_HOST_IP="$(bash "${SCRIPT_DIR}/detect-webrtc-host-ip.sh")"
    fi
    export WEBRTC_HOST_IP
}

do_up() {
    detect_webrtc_host_ip
    ./scripts/private/dev-init.sh pek-dev-rich "$DC_RICH" "$DEV_ENV_FILE"

    echo "Using WebRTC host IP: ${WEBRTC_HOST_IP}"
    echo "Using WebRTC TURN relay ports: ${PEK_WEBRTC_TURN_MIN_PORT}-${PEK_WEBRTC_TURN_MAX_PORT}"

    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" WEBRTC_HOST_IP="${WEBRTC_HOST_IP}" \
        PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT}" \
        PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT}" \
        docker compose "${COMPOSE_FILES[@]}" up -d --build
}

do_down() {
    HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" WEBRTC_HOST_IP="${WEBRTC_HOST_IP:-}" \
        PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT}" \
        PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT}" \
        docker compose "${COMPOSE_FILES[@]}" down
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
