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
  start-container.sh [--recreate] [--no-build] [--env-file PATH] [-h|--help]

Builds and starts the PEK quick-start container selected by host detection.

Platform mapping:
  Raspberry Pi 5            -> pek-dev with Pi camera support
  WSL/Linux/macOS           -> pek-dev

The script uses the checked-in devcontainer compose files and generated device
overrides.

Options:
  --recreate  Recreate the selected container even if it is already running
  --no-build  Start from an image already present in Docker
  --env-file PATH
              Pass PATH to Docker Compose for variable interpolation
EOF
}

RECREATE="false"
BUILD="true"
COMPOSE_ENV_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --recreate)
            RECREATE="true"
            ;;
        --no-build)
            BUILD="false"
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
export HF_TOKEN="${HF_TOKEN-}"
export HF_DOWNLOAD_CACHEBUST
HF_DOWNLOAD_CACHEBUST="$("${REPO_ROOT}/scripts/private/generate-hf-download-cachebust.sh")"
CONTAINER_READY_FILE="/tmp/pek-development-entrypoint-ready"

COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
    -f .devcontainer/docker-compose.ssh-agent.yaml
    -f .devcontainer/docker-compose.devcont.video.yaml
    -f .devcontainer/docker-compose.devcont.audio.yaml
    -f .devcontainer/docker-compose.devcont.shared_memory.yaml
)

COMPOSE_COMMAND=(docker compose)
if [[ -n "${COMPOSE_ENV_FILE}" ]]; then
    COMPOSE_COMMAND+=(--env-file "${COMPOSE_ENV_FILE}")
fi

PEK_WEBRTC_TURN="$(bash scripts/private/select-webrtc-turn-mode.sh "${PEK_PLATFORM_ID}")"
if [[ -n "${PEK_DEV_CACHE_FROM:-}" ]]; then
    COMPOSE_FILES+=(-f .devcontainer/docker-compose.devcont.registry-cache.yaml)
fi
if [[ "${PEK_PLATFORM_ID}" == macos ]]; then
    COMPOSE_FILES+=(-f .devcontainer/docker-compose.devcont.macos-cache.yaml)
fi
if [[ "${PEK_WEBRTC_TURN}" == enabled ]]; then
    COMPOSE_FILES+=(-f .devcontainer/docker-compose.devcont.turn.yaml)
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

container_ready() {
    docker exec "${PEK_CONTAINER_NAME}" \
        test -f "${CONTAINER_READY_FILE}" > /dev/null 2>&1
}

wait_for_container_ready() {
    local attempt

    for ((attempt = 0; attempt < 300; attempt++)); do
        if ! container_running; then
            echo "Error: quick-start container exited during startup: ${PEK_CONTAINER_NAME}" >&2
            docker logs "${PEK_CONTAINER_NAME}" >&2 || true
            return 1
        fi
        if container_ready; then
            return 0
        fi
        sleep 1
    done

    echo "Error: quick-start container did not become ready within 300 seconds: ${PEK_CONTAINER_NAME}" >&2
    docker logs "${PEK_CONTAINER_NAME}" >&2 || true
    return 1
}

container_state_writable() {
    local ccache_dir path project_root
    project_root="$(docker exec -u dev "${PEK_CONTAINER_NAME}" \
        sh -c 'printf %s "${PEK_PROJECT_ROOT:-$PWD}"')"
    ccache_dir="$(docker exec -u dev "${PEK_CONTAINER_NAME}" \
        sh -c 'printf %s "${CCACHE_DIR:-${PEK_PROJECT_ROOT:-$PWD}/.cache/ccache}"')"
    if ! docker exec -u dev "${PEK_CONTAINER_NAME}" \
        mkdir -p "${ccache_dir}" "${project_root}/development/build"; then
        echo "Cannot create development cache directories as dev." >&2
        return 1
    fi
    for path in "${project_root}" "${ccache_dir}" "${project_root}/development/build"; do
        if ! docker exec -u dev "${PEK_CONTAINER_NAME}" test -w "${path}"; then
            echo "Not writable as dev: ${path}" >&2
            return 1
        fi
    done
    if docker exec -u dev "${PEK_CONTAINER_NAME}" test -e /home/dev/.bash_profile &&
        ! docker exec -u dev "${PEK_CONTAINER_NAME}" test -r /home/dev/.bash_profile; then
        echo "Not readable as dev: /home/dev/.bash_profile" >&2
        return 1
    fi
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

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"
export HOST_UID HOST_GID

require_docker

echo
echo "Generating device overrides..."
bash .devcontainer/platform_init.sh \
    "${PEK_CONTAINER_SERVICE}" "${PEK_PICAMERA}" "${PEK_WEBRTC_TURN}"

if container_running && [[ "$RECREATE" != "true" ]]; then
    if container_ready && container_state_writable; then
        print_enter_hint
        exit 0
    fi

    echo "Container is running, but startup readiness or project-root writability was not confirmed."
    echo "Recreating it with the host UID/GID mapping..."
    RECREATE="true"
fi

echo "Starting quick-start container:"
echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
echo "  Service:  ${PEK_CONTAINER_SERVICE}"
echo "  Name:     ${PEK_CONTAINER_NAME}"
if [[ "$BUILD" == "true" ]]; then
    echo
    echo "Building shared development base..."
    bash scripts/private/build-dev-base.sh
fi

echo
echo "Building and starting container..."
UP_ARGS=(up -d --remove-orphans)
if [[ "$BUILD" == "true" ]]; then
    UP_ARGS+=(--build)
else
    UP_ARGS+=(--no-build)
fi
if [[ "$RECREATE" == "true" ]]; then
    UP_ARGS+=(--force-recreate)
fi
"${COMPOSE_COMMAND[@]}" "${COMPOSE_FILES[@]}" "${UP_ARGS[@]}" "${PEK_CONTAINER_SERVICE}"
wait_for_container_ready
if ! container_state_writable; then
    echo "Error: container development directories are not writable as dev." >&2
    exit 1
fi

echo
docker ps --filter "name=${PEK_CONTAINER_NAME}" --format 'table {{.Names}} {{.Status}}'

echo
print_enter_hint
