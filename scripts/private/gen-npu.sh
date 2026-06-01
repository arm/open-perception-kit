#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  gen-npu.sh <service_name> <out_compose_yaml> <out_env>

Generates a docker-compose override that maps /dev/hailo* devices into the
given service, and appends NPU env vars into <out_env>.
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

touch "${OUT_ENV}"

HAILORT_UDS_SOCK="/tmp/hailort_uds.sock"
ALL_HAILO_DEVS=()

IFS=$'\n'
read -r -d '' -a ALL_HAILO_DEVS < <(
    ls -1 /dev/hailo* 2>/dev/null |
        sort -V || true
    printf '\0'
)

for i in "${!ALL_HAILO_DEVS[@]}"; do
    echo "NPU${i}=${ALL_HAILO_DEVS[$i]}" >>"$OUT_ENV"
done
echo "NPU_COUNT=${#ALL_HAILO_DEVS[@]}" >>"$OUT_ENV"

{
    echo "services:"
    echo "  ${SERVICE_NAME}:"

    if [[ -S "$HAILORT_UDS_SOCK" ]]; then
        echo "    volumes:"
        echo "      - ${HAILORT_UDS_SOCK}:${HAILORT_UDS_SOCK}"
    fi

    if [[ ${#ALL_HAILO_DEVS[@]} -eq 0 ]]; then
        echo "    devices: []"
        echo "No NPU devices found. Generated empty NPU override." >&2
    else
        echo "    devices:"
        for DEV in "${ALL_HAILO_DEVS[@]}"; do
            echo "      - ${DEV}:${DEV}"
        done
        echo "Generated NPU mapping for ${#ALL_HAILO_DEVS[@]} device(s)." >&2
    fi
} >"${OUT_COMPOSE}"
