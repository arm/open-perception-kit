#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  gen_audio.sh <service_name> <out_compose_yaml> <out_env>

Generates a docker-compose override that maps ALSA /dev/snd character devices
into the given service, and appends microphone env vars into <out_env>.
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

SND_DIR="/dev/snd"

touch "${OUT_ENV}"

if [[ -d "$SND_DIR" ]]; then
    mapfile -t ALL_SND_DEVS < <(find -L "$SND_DIR" -maxdepth 1 -type c -print 2> /dev/null | sort -V || true)
    mapfile -t MIC_DEVS < <(find -L "$SND_DIR" -maxdepth 1 -type c -name 'pcmC*D*c' -print 2> /dev/null | sort -V || true)
else
    ALL_SND_DEVS=()
    MIC_DEVS=()
fi

for i in "${!MIC_DEVS[@]}"; do
    echo "MIC${i}=${MIC_DEVS[$i]}" >> "$OUT_ENV"
done
echo "MIC_COUNT=${#MIC_DEVS[@]}" >> "$OUT_ENV"

{
    echo "services:"
    echo "  ${SERVICE_NAME}:"

    if [[ ${#ALL_SND_DEVS[@]} -eq 0 ]]; then
        echo "    devices: []"
        echo "No ALSA devices found. Generated empty audio override." >&2
    else
        echo "    devices:"
        for DEV in "${ALL_SND_DEVS[@]}"; do
            echo "      - ${DEV}:${DEV}"
        done
        echo "Generated microphone mapping for ${#MIC_DEVS[@]} capture device(s)." >&2
    fi
} > "${OUT_COMPOSE}"
