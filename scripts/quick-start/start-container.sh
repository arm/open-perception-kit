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
  Raspberry Pi 5            -> pek-dev with Pi camera support
  Raspberry Pi 5 + Hailo 8  -> pek-dev-rpi5-h8
  Raspberry Pi 5 + Hailo 10 -> pek-dev-rpi5-h10
  WSL/Linux/macOS           -> pek-dev

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
export PEK_DEV_CONTAINER_NAME PEK_DEV_RPI5_H8_CONTAINER_NAME
export PEK_DEV_RPI5_H10_CONTAINER_NAME PEK_PICAMERA

COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
    -f .devcontainer/docker-compose.devcont.video.yaml
    -f .devcontainer/docker-compose.devcont.audio.yaml
    -f .devcontainer/docker-compose.devcont.npu.yaml
    -f .devcontainer/docker-compose.devcont.shared_memory.yaml
)

COMPOSE_COMMAND=(docker compose)
if [[ -n "${COMPOSE_ENV_FILE}" ]]; then
    COMPOSE_COMMAND+=(--env-file "${COMPOSE_ENV_FILE}")
fi

PEK_WEBRTC_TURN="$(bash scripts/private/select-webrtc-turn-mode.sh "${PEK_PLATFORM_ID}")"
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

container_workdir_writable() {
    docker exec -u dev "${PEK_CONTAINER_NAME}" bash -lc 'test -w /work' > /dev/null 2>&1
}

expected_modelfetch_sdk_sha256() {
    local machine="$1"
    local architecture
    case "$machine" in
        x86_64)
            architecture="amd64"
            ;;
        aarch64)
            architecture="arm64"
            ;;
        *)
            echo "Unsupported container architecture: ${machine}" >&2
            return 1
            ;;
    esac
    "${REPO_ROOT}/scripts/private/read-modelfetch-release-manifest.sh" \
        "${REPO_ROOT}/scripts/private/modelfetch-release.manifest" \
        "${architecture}_sha256"
}

container_has_current_modelfetch_sdk() {
    local container_machine expected_sha
    container_machine="$(docker exec -u dev "${PEK_CONTAINER_NAME}" uname -m)" || return 1
    expected_sha="$(expected_modelfetch_sdk_sha256 "$container_machine")" || return 1
    docker exec -u dev "${PEK_CONTAINER_NAME}" sh -c '
        test -f /opt/pek-deps/modelfetch/include/modelfetch/modelfetch.hpp &&
        test -f /opt/pek-deps/modelfetch/lib/libmodelfetch_c.so &&
        test "$(cat /opt/pek-deps/modelfetch/.release-sdk-sha256 2>/dev/null)" = "$1"
    ' _ "$expected_sha" > /dev/null 2>&1
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

echo
echo "Generating device overrides..."
bash .devcontainer/platform_init.sh \
    "${PEK_CONTAINER_SERVICE}" "${PEK_PICAMERA}" "${PEK_WEBRTC_TURN}"

if container_running && [[ "$RECREATE" != "true" ]]; then
    if container_workdir_writable && container_has_current_modelfetch_sdk; then
        echo "Reconciling the running container with the current Compose environment..."
        "${COMPOSE_COMMAND[@]}" "${COMPOSE_FILES[@]}" \
            up -d --no-build --remove-orphans "${PEK_CONTAINER_SERVICE}"
        print_enter_hint
        exit 0
    fi

    echo "The running container is missing the current workspace contract."
    echo "Recreating it with the host UID/GID mapping and modelfetch C++ SDK..."
    RECREATE="true"
fi

echo "Starting quick-start container:"
echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
echo "  Service:  ${PEK_CONTAINER_SERVICE}"
echo "  Name:     ${PEK_CONTAINER_NAME}"
if [[ "${PEK_PLATFORM_ID}" == rpi5* ]]; then
    echo "  Hailo:    ${PEK_HAILO_ARCH}"
fi

echo
echo "Building shared development base..."
bash scripts/private/prepare-modelfetch-release.sh > /dev/null
bash scripts/private/build-dev-base.sh

echo
echo "Building and starting container..."
UP_ARGS=(up -d --build --remove-orphans)
if [[ "$RECREATE" == "true" ]]; then
    UP_ARGS+=(--force-recreate)
fi
"${COMPOSE_COMMAND[@]}" "${COMPOSE_FILES[@]}" "${UP_ARGS[@]}" "${PEK_CONTAINER_SERVICE}"

echo
docker ps --filter "name=${PEK_CONTAINER_NAME}" --format 'table {{.Names}} {{.Status}}'

echo
print_enter_hint
