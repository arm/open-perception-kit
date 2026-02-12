#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CAM_ENV_FILE=".devcontainer/cameras.env"
DC_DEVCONT=".devcontainer/docker-compose.devcont.video.yaml"
DC_RICH=".devcontainer/docker-compose.video.yaml"

cd "${REPO_ROOT}"

./scripts/gen_cam.sh "$DC_DEVCONT" "$DC_RICH" "$CAM_ENV_FILE"

docker exec -it -u devgoblin --env-file "${CAM_ENV_FILE}" -e TERM="$TERM" amp-dev-rich zsh

