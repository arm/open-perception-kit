#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail
# Ensure we are in the scripts directory

PEK_PIPELINE=${PEK_PIPELINE:-"yolov11-onnx"}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../../

sudo chown -R $(id -u):$(id -g) "/work/" || true
sudo chmod +x ./tools/pek-menu ./scripts/serve-docs-plain.sh ./scripts/build.sh ./scripts/gen-doc.sh ./.devcontainer/setup.sh ./.devcontainer/platform_init.sh 2> /dev/null || true

.devcontainer/setup.sh
.devcontainer/platform_init.sh pek-dev disabled
./scripts/build.sh clean
./scripts/build.sh debug false
./scripts/gen-doc.sh

# Run scripts and redirect output to container log
./scripts/serve-docs-plain.sh &
./tools/pek-menu "$PEK_PIPELINE"

while true; do wait; done
