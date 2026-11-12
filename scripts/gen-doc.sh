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

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${OPK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
if [[ "$PROJECT_ROOT" != /* || ! -d "$PROJECT_ROOT/docs/public" ]]; then
    echo "OPK_PROJECT_ROOT must be an absolute OPK checkout path." >&2
    exit 2
fi
PROJECT_ROOT="$(cd -- "$PROJECT_ROOT" && pwd -P)"

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    echo "Usage: $0 [--with-api | --prepare-publish]"
    echo "Generate docs/html with the pinned documentation container. Sources are read-only."
    echo "--prepare-publish writes public Markdown and diagram assets to docs/prepared-public."
    exit 0
fi

OUTPUT_DIRECTORY="$PROJECT_ROOT/docs/html"
for argument in "$@"; do
    case "$argument" in
        --prepare-publish) OUTPUT_DIRECTORY="$PROJECT_ROOT/docs/prepared-public" ;;
        --with-api) ;;
        *)
            echo "Unknown argument: $argument. Use --help for usage." >&2
            exit 2
            ;;
    esac
done

if command -v podman > /dev/null 2>&1; then
    CONTAINER_ENGINE=podman
    CONTAINER_USER_ARGS=(--userns=keep-id)
elif command -v docker > /dev/null 2>&1; then
    CONTAINER_ENGINE=docker
    CONTAINER_USER_ARGS=(--user "$(id -u):$(id -g)")
else
    echo "Install Docker or Podman to generate documentation." >&2
    exit 1
fi

if [[ -z "${DOCS_IMAGE:-}" ]]; then
    DOCS_IMAGE=opk-docs:local
    "$CONTAINER_ENGINE" build --build-arg NPM_REGISTRY --target opk-docs --tag "$DOCS_IMAGE" "$PROJECT_ROOT"
fi
mkdir -p "$OUTPUT_DIRECTORY" "$PROJECT_ROOT/.cache/docs"
PUBLISHED_COMMIT="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
exec "$CONTAINER_ENGINE" run --rm "${CONTAINER_USER_ARGS[@]}" \
    --env HOME=/tmp --env "OPK_DOCS_COMMIT=$PUBLISHED_COMMIT" \
    --env PNPM_CONFIG_STORE_DIR=/tmp/.local/share/pnpm/store \
    --env PNPM_CONFIG_CACHE_DIR=/tmp/.local/share/pnpm/cache \
    --volume "$PROJECT_ROOT:/source:ro" \
    --volume "$OUTPUT_DIRECTORY:/output" \
    --volume "$PROJECT_ROOT/.cache/docs:/tmp/.local/share/pnpm" \
    "$DOCS_IMAGE" python3 /source/scripts/private/generate_docs.py "$@"
