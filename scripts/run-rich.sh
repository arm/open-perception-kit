#!/usr/bin/env bash
set -euo pipefail

# Ensure we run from repo root even if script is called elsewhere
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CAM_ENV_FILE=".devcontainer/cameras.env"
DC_DEVCONT=".devcontainer/docker-compose.devcont.video.yaml"
DC_RICH=".devcontainer/docker-compose.video.yaml"

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

cd "${REPO_ROOT}"

./scripts/gen_cam.sh "$DC_DEVCONT" "$DC_RICH" "$CAM_ENV_FILE"

HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" \
docker compose -f .devcontainer/docker-compose.yaml -f .devcontainer/docker-compose.video.yaml up -d --build
