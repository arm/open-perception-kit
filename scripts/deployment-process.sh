#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# Ensure we are in the scripts directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../

sudo chown -R $(id -u):$(id -g) "/work/" || true
sudo chmod +x ./scripts/amp-menu ./scripts/serve-docs.sh ./scripts/build-elements.sh ./scripts/gendoc.sh ./.devcontainer/setup.sh ./.devcontainer/platform_init.sh 2> /dev/null || true

.devcontainer/setup.sh
.devcontainer/platform_init.sh amp-dev-base
./scripts/build-elements.sh clean
./scripts/build-elements.sh debug false
./scripts/gendoc.sh

# Run scripts and redirect output to container log
./scripts/serve-docs.sh &
./scripts/amp-menu onnx

while true; do wait; done
