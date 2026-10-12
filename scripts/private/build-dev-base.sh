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
# Builds the shared base image used by the consolidated Dockerfile targets.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${REPO_ROOT}"
if ! docker buildx version > /dev/null 2>&1; then
    echo "Error: Docker Buildx is required for OPK's BuildKit image builds." >&2
    echo "Install Buildx so 'docker buildx version' works, then rerun the build." >&2
    echo "Installation: https://github.com/docker/buildx#installing" >&2
    exit 1
fi

exec docker compose -f .devcontainer/compose.devcont.yaml build opk-build-base
