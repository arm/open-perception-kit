#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if (($# != 0)); then
    echo "prepare-modelfetch-release-in-container.sh does not accept arguments" >&2
    exit 2
fi

: "${GH_TOKEN:?GH_TOKEN is required to prepare the modelfetch release}"
: "${MODELFETCH_RELEASE_TOOL_IMAGE:?MODELFETCH_RELEASE_TOOL_IMAGE is required}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd -P)"
CACHE_PARENT="${REPO_ROOT}/.cache"
CACHE_ROOT="${CACHE_PARENT}/modelfetch"

if [[ -L "$CACHE_PARENT" || -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch release cache path: ${CACHE_ROOT}" >&2
    exit 1
fi
mkdir -p "$CACHE_ROOT"
if [[ ! -d "$CACHE_PARENT" || -L "$CACHE_PARENT" ||
    ! -d "$CACHE_ROOT" || -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch release cache path: ${CACHE_ROOT}" >&2
    exit 1
fi

cleanup_tool_image() {
    docker image rm --force "$MODELFETCH_RELEASE_TOOL_IMAGE" > /dev/null 2>&1 || true
}
trap cleanup_tool_image EXIT

docker build \
    --file "${SCRIPT_DIR}/modelfetch-release-tools.Dockerfile" \
    --tag "$MODELFETCH_RELEASE_TOOL_IMAGE" \
    "$REPO_ROOT"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,source=${REPO_ROOT},target=/workspace,readonly" \
    --mount "type=bind,source=${CACHE_ROOT},target=/modelfetch-cache" \
    --workdir /workspace \
    --env GH_TOKEN \
    --env GH_CONFIG_DIR=/tmp/gh-config \
    --env HOME=/tmp \
    --env MODELFETCH_CACHE_ROOT=/modelfetch-cache \
    "$MODELFETCH_RELEASE_TOOL_IMAGE" \
    bash scripts/private/prepare-modelfetch-release.sh > /dev/null
