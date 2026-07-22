#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# This runs on the *host* (before the container is created).
# Generate docker-compose override(s) for camera/device passthrough.

TARGET_SERVICE_KIND="${1:-pek-dev}"
PEK_PICAMERA="${2:-disabled}"
PEK_WEBRTC_TURN="${3:-disabled}"
PEK_BUILD_BASE_IMAGE="${PEK_BUILD_BASE_IMAGE:-${PEK_DEV_CONTAINER_NAME:-perception-experience-kit}-build-base}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ "${PEK_WEBRTC_TURN}" == auto ]]; then
    PEK_WEBRTC_TURN="$(bash scripts/private/select-webrtc-turn-mode.sh)"
fi

case "${PEK_PICAMERA}" in
    enabled | disabled) ;;
    *)
        echo "Error: PEK_PICAMERA must be 'enabled' or 'disabled'." >&2
        exit 2
        ;;
esac

case "${PEK_WEBRTC_TURN}" in
    enabled | disabled) ;;
    *)
        echo "Error: PEK_WEBRTC_TURN must be 'enabled' or 'disabled'." >&2
        exit 2
        ;;
esac

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

remove_env_value() {
    local file="$1"
    local key="$2"

    [[ -f "$file" ]] || return
    sed -i.bak "/^${key}=/d" "$file"
    rm -f "${file}.bak"
}

chmod +x scripts/private/dev-init.sh || true
touch devices.env
bash ./scripts/private/dev-init.sh "${TARGET_SERVICE_KIND}" devcont devices.env

NETWORK_OVERRIDE=".devcontainer/docker-compose.devcont.network.yaml"
if [[ "${PEK_WEBRTC_TURN}" = enabled ]]; then
    cp .devcontainer/docker-compose.devcont.turn.yaml "${NETWORK_OVERRIDE}"
else
    printf "services:\n  %s: {}\n" "${TARGET_SERVICE_KIND}" > "${NETWORK_OVERRIDE}"
fi

upsert_env_value .env PEK_PICAMERA "$PEK_PICAMERA"
upsert_env_value .devcontainer/.env PEK_PICAMERA "$PEK_PICAMERA"
upsert_env_value devices.env PEK_PICAMERA "$PEK_PICAMERA"
upsert_env_value .env PEK_BUILD_BASE_IMAGE "$PEK_BUILD_BASE_IMAGE"
upsert_env_value .devcontainer/.env PEK_BUILD_BASE_IMAGE "$PEK_BUILD_BASE_IMAGE"
upsert_env_value devices.env PEK_BUILD_BASE_IMAGE "$PEK_BUILD_BASE_IMAGE"

if [[ "${PEK_WEBRTC_TURN}" = enabled ]]; then
    chmod +x scripts/private/detect-webrtc-host-ip.sh || true
    WEBRTC_HOST_IP="${WEBRTC_HOST_IP:-"$(./scripts/private/detect-webrtc-host-ip.sh)"}"
    PEK_WEBRTC_TURN_MIN_PORT="${PEK_WEBRTC_TURN_MIN_PORT:-49000}"
    PEK_WEBRTC_TURN_MAX_PORT="${PEK_WEBRTC_TURN_MAX_PORT:-49050}"

    for env_file in .env .devcontainer/.env devices.env; do
        upsert_env_value "$env_file" WEBRTC_HOST_IP "$WEBRTC_HOST_IP"
        upsert_env_value "$env_file" PEK_WEBRTC_TURN_MIN_PORT "$PEK_WEBRTC_TURN_MIN_PORT"
        upsert_env_value "$env_file" PEK_WEBRTC_TURN_MAX_PORT "$PEK_WEBRTC_TURN_MAX_PORT"
    done

    echo "Using WebRTC host IP: ${WEBRTC_HOST_IP}"
    echo "Using WebRTC TURN relay ports: ${PEK_WEBRTC_TURN_MIN_PORT}-${PEK_WEBRTC_TURN_MAX_PORT}"
else
    for env_file in .env .devcontainer/.env devices.env; do
        remove_env_value "$env_file" WEBRTC_HOST_IP
        remove_env_value "$env_file" PEK_WEBRTC_TURN_MIN_PORT
        remove_env_value "$env_file" PEK_WEBRTC_TURN_MAX_PORT
    done
fi
