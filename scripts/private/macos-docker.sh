#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################
# Prepares the persistent macOS runner for an isolated Colima CI run.

set -euo pipefail

brew install docker colima docker-compose docker-buildx qemu
mkdir -p "${HOME}/.docker/cli-plugins"
ln -sf "$(brew --prefix)/opt/docker-compose/bin/docker-compose" "${HOME}/.docker/cli-plugins/docker-compose"
ln -sf "$(brew --prefix)/opt/docker-buildx/bin/docker-buildx" "${HOME}/.docker/cli-plugins/docker-buildx"

colima start --vm-type=qemu --cpu 4 --memory 8 --mount-type=sshfs
docker context use colima
./scripts/quick-start/detect-environment.sh
docker info
docker compose version
