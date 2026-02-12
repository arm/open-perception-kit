#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${REPO_ROOT}"

CAM_ENV_FILE=".devcontainer/cameras.env"

./scripts/gen_cam.sh

docker exec -it -u devgoblin --env-file "${CAM_ENV_FILE}" -e TERM="$TERM" amp-dev-rich zsh

