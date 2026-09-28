#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
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
