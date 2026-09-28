#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
	dev-init.sh <service_name> <container_kind> <out_env>

    Generates device docker-compose overrides (video/audio/shared memory) and a shared env
file for the given service.

container_kind controls the override filenames:
        .devcontainer/docker-compose.<container_kind>.video.yaml
        .devcontainer/docker-compose.<container_kind>.audio.yaml
        .devcontainer/docker-compose.<container_kind>.shared_memory.yaml
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
    usage
    exit 0
fi

TARGET_SERVICE_NAME="${1:-}"
TARGET_CONTAINER_KIND="${2:-}"
OUT_ENV_FILE="${3:-}"

if [[ -z "$TARGET_SERVICE_NAME" || -z "$TARGET_CONTAINER_KIND" || -z "$OUT_ENV_FILE" ]]; then
    usage >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

chmod +x scripts/private/gen-cam.sh || true
bash ./scripts/private/gen-cam.sh "${TARGET_SERVICE_NAME}" \
    ".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.video.yaml" \
    "${OUT_ENV_FILE}"

chmod +x scripts/private/gen-audio.sh || true
bash ./scripts/private/gen-audio.sh "${TARGET_SERVICE_NAME}" \
    ".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.audio.yaml" \
    "${OUT_ENV_FILE}"

chmod +x scripts/private/gen-shared-memory.sh || true
bash ./scripts/private/gen-shared-memory.sh "${TARGET_SERVICE_NAME}" \
    ".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.shared_memory.yaml" \
    "${OUT_ENV_FILE}"
