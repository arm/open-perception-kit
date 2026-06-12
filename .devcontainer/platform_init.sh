#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# This runs on the *host* (before the container is created).
# Generate docker-compose override(s) for camera/device passthrough.

TARGET_SERVICE_KIND="${1:-pek-dev-base}"

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
chmod +x scripts/private/detect-webrtc-host-ip.sh || true
touch devices.env
bash ./scripts/private/dev-init.sh "${TARGET_SERVICE_KIND}" devcont devices.env

WEBRTC_HOST_IP="${WEBRTC_HOST_IP:-"$(./scripts/private/detect-webrtc-host-ip.sh)"}"
upsert_env_value .env WEBRTC_HOST_IP "$WEBRTC_HOST_IP"
upsert_env_value .devcontainer/.env WEBRTC_HOST_IP "$WEBRTC_HOST_IP"
upsert_env_value devices.env WEBRTC_HOST_IP "$WEBRTC_HOST_IP"

echo "Using WebRTC host IP: ${WEBRTC_HOST_IP}"
