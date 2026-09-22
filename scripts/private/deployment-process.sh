#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail
# Ensure we are in the scripts directory

OPK_PIPELINE=${OPK_PIPELINE:-"yolov11-onnx"}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../../

sudo chown -R $(id -u):$(id -g) "/work/" || true
sudo chmod +x ./tools/opk-menu ./scripts/serve-docs-plain.sh ./scripts/build.sh ./scripts/gen-doc.sh ./.devcontainer/setup.sh ./.devcontainer/platform_init.sh 2> /dev/null || true

.devcontainer/setup.sh
.devcontainer/platform_init.sh opk-dev disabled
./scripts/build.sh clean
./scripts/build.sh debug false
./scripts/gen-doc.sh

# Run scripts and redirect output to container log
./scripts/serve-docs-plain.sh &
./tools/opk-menu "$OPK_PIPELINE"

while true; do wait; done
