#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

IMAGE="ghcr.io/arm-debug/edge-ai-docs/local-development:v0.0.2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCS_DIR="$REPO_ROOT/docs"

if command -v podman > /dev/null 2>&1; then
    CONTAINER_ENGINE="podman"
elif command -v docker > /dev/null 2>&1; then
    CONTAINER_ENGINE="docker"
else
    cat << 'EOF'
Neither podman nor docker is installed.

serve-docs.sh requires one of these container engines to run the local docs image.
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

if [ ! -d "$DOCS_DIR" ]; then
    printf 'Documentation directory not found: %s\n' "$DOCS_DIR" >&2
    exit 1
fi

printf 'Serving docs with %s on http://localhost:3003\n' "$CONTAINER_ENGINE"
exec "$CONTAINER_ENGINE" run --rm -it -p 3003:3000 -v "$DOCS_DIR:/opt/docusaurus/content" "$IMAGE"
