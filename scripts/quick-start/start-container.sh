#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Builds and starts the quick-start development container for the detected host.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  start-container.sh [--recreate] [--env-file PATH] [-h|--help]

Builds and starts the PEK quick-start container selected by host detection.

Platform mapping:
  Raspberry Pi 5         -> pek-dev with Pi camera support
  WSL/Linux/macOS        -> pek-dev

The script uses the checked-in devcontainer compose files and generated device
overrides.

Options:
  --recreate  Recreate the selected container even if it is already running
  --env-file PATH
              Pass PATH to Docker Compose for variable interpolation
EOF
}

RECREATE="false"
COMPOSE_ENV_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --recreate)
            RECREATE="true"
            ;;
        --env-file)
            if [[ $# -lt 2 ]]; then
                echo "Error: --env-file requires a path." >&2
                exit 2
            fi
            COMPOSE_ENV_FILE="$2"
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown argument '${1}'" >&2
            echo >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if ! detect_output="$("${SCRIPT_DIR}/detect-environment.sh" --shell)"; then
    eval "$detect_output"
    echo "Error: unsupported quick-start platform: ${PEK_PLATFORM_NAME:-unknown}" >&2
    if [[ -n "${PEK_UNSUPPORTED_REASON:-}" ]]; then
        echo "Reason: ${PEK_UNSUPPORTED_REASON}" >&2
    fi
    exit 1
fi
eval "$detect_output"
export PEK_DEV_CONTAINER_NAME PEK_PICAMERA

COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
    -f .devcontainer/docker-compose.devcont.video.yaml
    -f .devcontainer/docker-compose.devcont.audio.yaml
    -f .devcontainer/docker-compose.devcont.shared_memory.yaml
)

COMPOSE_ENV_ARGS=()
if [[ -n "${COMPOSE_ENV_FILE}" ]]; then
    COMPOSE_ENV_ARGS=(--env-file "${COMPOSE_ENV_FILE}")
fi

require_docker() {
    if ! command -v docker > /dev/null 2>&1; then
        echo "Error: docker is not installed or not on PATH." >&2
        echo "Install Docker for this platform, then rerun ./scripts/quick_start.sh." >&2
        exit 1
    fi

    if ! docker compose version > /dev/null 2>&1; then
        echo "Error: Docker Compose plugin is not available." >&2
        echo "Install the Docker Compose plugin so 'docker compose' works, then rerun ./scripts/quick_start.sh." >&2
        exit 1
    fi

    if ! docker info > /dev/null 2>&1; then
        echo "Error: Docker is installed but not usable by this user." >&2
        echo "Start Docker and confirm 'docker info' works without sudo, then rerun ./scripts/quick_start.sh." >&2
        if [[ "${PEK_PLATFORM_ID}" == rpi5* || "${PEK_PLATFORM_ID}" == "linux-x86_64" ]]; then
            echo "On Linux, this often means running: sudo usermod -aG docker \"\$USER\""
            echo "After that, log out and log back in."
        fi
        exit 1
    fi
}

container_running() {
    docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'
}

container_workdir_writable() {
    docker exec -u dev "${PEK_CONTAINER_NAME}" bash -lc 'test -w /work' > /dev/null 2>&1
}

print_enter_hint() {
    echo "Container is running: ${PEK_CONTAINER_NAME}"
    echo "Enter it with:"
    echo "  ./scripts/enter_cli.sh"
    echo "       or"
    if [[ -f "${REPO_ROOT}/devices.env" ]]; then
        echo "  docker exec -it -u dev --env-file devices.env -e TERM=\"\$TERM\" ${PEK_CONTAINER_NAME} bash"
    else
        echo "  docker exec -it -u dev -e TERM=\"\$TERM\" ${PEK_CONTAINER_NAME} bash"
    fi
}

cd "${REPO_ROOT}"

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"

require_docker

if container_running && [[ "$RECREATE" != "true" ]]; then
    if container_workdir_writable; then
        print_enter_hint
        exit 0
    fi

    echo "Container is running, but /work is not writable as dev."
    echo "Recreating it with the host UID/GID mapping..."
    RECREATE="true"
fi

echo "Starting quick-start container:"
echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
echo "  Service:  ${PEK_CONTAINER_SERVICE}"
echo "  Name:     ${PEK_CONTAINER_NAME}"

echo
echo "Generating device overrides..."
bash .devcontainer/platform_init.sh "${PEK_CONTAINER_SERVICE}" "${PEK_PICAMERA}"

echo
echo "Building shared development base..."
bash scripts/private/build-dev-base.sh

echo
echo "Building and starting container..."
UP_ARGS=(up -d --build --remove-orphans)
if [[ "$RECREATE" == "true" ]]; then
    UP_ARGS+=(--force-recreate)
fi
docker compose "${COMPOSE_ENV_ARGS[@]}" "${COMPOSE_FILES[@]}" "${UP_ARGS[@]}" "${PEK_CONTAINER_SERVICE}"

echo
docker ps --filter "name=${PEK_CONTAINER_NAME}" --format 'table {{.Names}} {{.Status}}'

echo
print_enter_hint
