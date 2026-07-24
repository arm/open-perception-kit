#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
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
    export PEK_DEV_BASE_CONTAINER_NAME PEK_DEV_RPI5_CONTAINER_NAME PEK_DEV_RPI5_H8_CONTAINER_NAME PEK_DEV_RPI5_H10_CONTAINER_NAME
fi

for compose_file in \
    .devcontainer/docker-compose.devcont.video.yaml \
    .devcontainer/docker-compose.devcont.audio.yaml \
    .devcontainer/docker-compose.devcont.npu.yaml \
    .devcontainer/docker-compose.devcont.shared_memory.yaml; do
    [ ! -f "$compose_file" ] || COMPOSE_FILES+=(-f "$compose_file")
done

if docker compose version > /dev/null 2>&1; then
    DOWN_ARGS=(down --remove-orphans)
    if [[ "${CI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
        DOWN_ARGS+=(--rmi local)
    fi
    docker compose "${COMPOSE_FILES[@]}" "${DOWN_ARGS[@]}"
fi
