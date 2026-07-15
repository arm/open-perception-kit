#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# This runs on the *host* (before the container is created).
# Generate docker-compose override(s) for camera/device passthrough.

TARGET_SERVICE_KIND="${1:-pek-dev}"
PEK_PICAMERA="${2:-disabled}"
PEK_BUILD_BASE_IMAGE="${PEK_BUILD_BASE_IMAGE:-${PEK_DEV_CONTAINER_NAME:-perception-experience-kit}-build-base}"

case "${PEK_PICAMERA}" in
    enabled | disabled) ;;
    *)
        echo "Error: PEK_PICAMERA must be 'enabled' or 'disabled'." >&2
        exit 2
        ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

upsert_env_value() {
    local file="$1"
    local key="$2"
    local value="$3"

    touch "$file"
    if grep -q "^${key}=" "$file"; then
        sed -i.bak "s|^${key}=.*|${key}=${value}|" "$file"
        rm -f "${file}.bak"
    else
        printf "%s=%s\n" "$key" "$value" >> "$file"
    fi
}

chmod +x scripts/private/dev-init.sh || true
touch devices.env
bash ./scripts/private/dev-init.sh "${TARGET_SERVICE_KIND}" devcont devices.env

upsert_env_value .env PEK_PICAMERA "$PEK_PICAMERA"
upsert_env_value .devcontainer/.env PEK_PICAMERA "$PEK_PICAMERA"
upsert_env_value devices.env PEK_PICAMERA "$PEK_PICAMERA"
upsert_env_value .env PEK_BUILD_BASE_IMAGE "$PEK_BUILD_BASE_IMAGE"
upsert_env_value .devcontainer/.env PEK_BUILD_BASE_IMAGE "$PEK_BUILD_BASE_IMAGE"
upsert_env_value devices.env PEK_BUILD_BASE_IMAGE "$PEK_BUILD_BASE_IMAGE"
