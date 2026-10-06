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

IMAGE="${DOCS_IMAGE:-ghcr.io/arm-debug/arm-docs-github-action/local:latest}"
PORT="${DOCS_PORT:-3003}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
requested_project_root="${OPK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
if [[ "$requested_project_root" != /* ]]; then
    echo "OPK_PROJECT_ROOT must be an absolute path: $requested_project_root" >&2
    exit 2
fi
if [[ ! -d "$requested_project_root" ]]; then
    echo "OPK project root does not exist: $requested_project_root" >&2
    exit 2
fi
OPK_PROJECT_ROOT="$(cd -- "$requested_project_root" && pwd -P)"
export OPK_PROJECT_ROOT
DOCS_ROOT_DIR="$OPK_PROJECT_ROOT/docs/public"
if [[ ! -d "$DOCS_ROOT_DIR" ]]; then
    printf 'Documentation root directory not found: %s\n' "$DOCS_ROOT_DIR" >&2
    exit 2
fi

if command -v podman > /dev/null 2>&1; then
    CONTAINER_ENGINE="podman"
elif command -v docker > /dev/null 2>&1; then
    CONTAINER_ENGINE="docker"
else
    cat << 'EOF'
Neither podman nor docker is installed.

serve-docs.sh requires one of these container engines to run the Arm docs preview image.
Install Podman or Docker, then run this script again.
EOF
    exit 1
fi

is_logged_in_to_ghcr() {
    if [ "$CONTAINER_ENGINE" = "podman" ]; then
        podman login ghcr.io --get-login > /dev/null 2>&1
        return
    fi

    local docker_config
    docker_config="${DOCKER_CONFIG:-$HOME/.docker}/config.json"

    if [ ! -f "$docker_config" ]; then
        return 1
    fi

    grep -Eq '"ghcr\.io"' "$docker_config"
}

if ! is_logged_in_to_ghcr; then
    cat << EOF
You must be logged into ghcr.io with $CONTAINER_ENGINE before serving the docs.

Required login process:
1. Create a GitHub Personal Access Token (classic).
2. Grant it the read:packages permission.
3. Run:

   $CONTAINER_ENGINE login ghcr.io

4. When prompted, provide:
   Username: your GitHub username
   Password: your GitHub PAT classic

If you are using podman directly, you can verify the login with:

   podman login ghcr.io --get-login

After the login succeeds, run this script again.
EOF
    exit 1
fi

if [ ! -f "$DOCS_ROOT_DIR/docs-config.json" ]; then
    printf 'Documentation config not found: %s\n' "$DOCS_ROOT_DIR/docs-config.json" >&2
    exit 1
fi

if [ ! -d "$DOCS_ROOT_DIR/static" ]; then
    printf 'Documentation static directory not found: %s\n' "$DOCS_ROOT_DIR/static" >&2
    exit 1
fi

container_args=(run --rm)

if [ -t 0 ] && [ -t 1 ]; then
    container_args+=(-it)
fi

container_args+=(-p "$PORT:3000")

container_args+=(--pull always)
container_args+=(-v "$DOCS_ROOT_DIR:/workspace/docs-site:ro")
container_args+=("$IMAGE")

printf 'Serving docs from %s with %s on http://localhost:%s\n' "$DOCS_ROOT_DIR" "$CONTAINER_ENGINE" "$PORT"
exec "$CONTAINER_ENGINE" "${container_args[@]}"
