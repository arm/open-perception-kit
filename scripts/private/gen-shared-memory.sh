#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
  gen-shared-memory.sh <service_name> <out_compose_yaml> <out_env>

Generates a docker-compose override that bind-mounts shared-memory / DMA related
paths that are commonly needed by NPUs:
  - /dev/dma_heap (directory)
  - /dev/udmabuf (device node, if present)
  - /dev/shm (POSIX shared memory filesystem)

The <out_env> argument is accepted for consistency with other generators but is
not currently modified.
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
    usage
    exit 0
fi

SERVICE_NAME="${1:-}"
OUT_COMPOSE="${2:-}"
OUT_ENV="${3:-}"

if [[ -z "$SERVICE_NAME" || -z "$OUT_COMPOSE" || -z "$OUT_ENV" ]]; then
    usage >&2
    exit 2
fi

# Keep signature consistent with other generators.
touch "${OUT_ENV}"

DMA_HEAP_DIR="/dev/dma_heap"
UDMABUF_DEV="/dev/udmabuf"
SHM_DIR="/dev/shm"

VOLUMES=()

if [[ -d "${DMA_HEAP_DIR}" ]]; then
    VOLUMES+=("${DMA_HEAP_DIR}:${DMA_HEAP_DIR}")
fi

if [[ -e "${UDMABUF_DEV}" ]]; then
    VOLUMES+=("${UDMABUF_DEV}:${UDMABUF_DEV}")
fi

if [[ -d "${SHM_DIR}" ]]; then
    VOLUMES+=("${SHM_DIR}:${SHM_DIR}")
fi

{
    echo "services:"
    echo "  ${SERVICE_NAME}:"

    if [[ ${#VOLUMES[@]} -eq 0 ]]; then
        echo "    volumes: []"
        echo "No shared-memory related paths found. Generated empty shared_memory override." >&2
    else
        echo "    volumes:"
        for SPEC in "${VOLUMES[@]}"; do
            echo "      - ${SPEC}"
        done
        echo "Generated shared_memory mounts: ${VOLUMES[*]}" >&2
    fi
} > "${OUT_COMPOSE}"
