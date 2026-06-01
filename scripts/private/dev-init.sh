#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
	cat <<'EOF'
Usage:
	dev-init.sh <service_name> <container_kind> <out_env>

Generates device docker-compose overrides (video/audio/npu) and a shared env
file for the given service.

container_kind controls the override filenames:
	.devcontainer/docker-compose.<container_kind>.video.yaml
	.devcontainer/docker-compose.<container_kind>.audio.yaml
	.devcontainer/docker-compose.<container_kind>.npu.yaml
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

chmod +x scripts/private/gen-npu.sh || true
bash ./scripts/private/gen-npu.sh "${TARGET_SERVICE_NAME}" \
	".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.npu.yaml" \
	"${OUT_ENV_FILE}"

chmod +x scripts/private/gen-shared-memory.sh || true
bash ./scripts/private/gen-shared-memory.sh "${TARGET_SERVICE_NAME}" \
	".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.shared_memory.yaml" \
	"${OUT_ENV_FILE}"
