#!/usr/bin/env bash
set -euo  pipefail

# Ensure we run from repo root even if script is called elsewhere
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}" && pwd)"

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

cd "${REPO_ROOT}"

HOST_UID="${HOST_UID}" HOST_GID="${HOST_GID}" \
docker compose -f .devcontainer/docker-compose.yaml up -d --build
