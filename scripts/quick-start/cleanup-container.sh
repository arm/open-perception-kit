#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################
# Stops quick-start containers for the current checkout.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

COMPOSE_FILES=(
    -f .devcontainer/compose.devcont.yaml
)

command -v docker > /dev/null 2>&1 || exit 0

cd "$REPO_ROOT"
detect_output="$("${SCRIPT_DIR}/detect-environment.sh" --shell 2> /dev/null)" || true
if [[ -n "$detect_output" ]]; then
    eval "$detect_output"
    export OPK_DEV_CONTAINER_NAME OPK_PICAMERA
    if [[ "${OPK_PLATFORM_ID}" == macos ]]; then
        HOST_UID="$(id -u)"
        HOST_GID="$(id -g)"
        export HOST_UID HOST_GID
        COMPOSE_FILES+=(-f .devcontainer/docker-compose.devcont.macos-cache.yaml)
    fi
    if [[ "$(bash scripts/private/select-webrtc-turn-mode.sh "${OPK_PLATFORM_ID}")" == enabled ]]; then
        COMPOSE_FILES+=(-f .devcontainer/docker-compose.devcont.turn.yaml)
    fi
fi
OPK_BUILD_BASE_IMAGE="${OPK_BUILD_BASE_IMAGE:-${OPK_DEV_CONTAINER_NAME:-open-perception-kit}-build-base}"

for compose_file in \
    .devcontainer/docker-compose.devcont.video.yaml \
    .devcontainer/docker-compose.devcont.audio.yaml \
    .devcontainer/docker-compose.devcont.shared_memory.yaml; do
    [ ! -f "$compose_file" ] || COMPOSE_FILES+=(-f "$compose_file")
done
if [[ -n "${XDG_RUNTIME_DIR:-}" && -d "${XDG_RUNTIME_DIR}" ]]; then
    COMPOSE_FILES+=(-f .devcontainer/docker-compose.devcont.wayland.yaml)
fi

if docker compose version > /dev/null 2>&1; then
    DOWN_ARGS=(down --remove-orphans)
    if [[ "${CI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
        DOWN_ARGS+=(--rmi local)
    fi
    docker compose "${COMPOSE_FILES[@]}" "${DOWN_ARGS[@]}"
fi

if [[ "${CI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
    docker image rm -f "${OPK_BUILD_BASE_IMAGE}" > /dev/null 2>&1 || true
fi
