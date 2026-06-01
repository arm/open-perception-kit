#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  gen-cam.sh <service_name> <out_compose_yaml> <out_env>

Generates a docker-compose override that maps camera-related /dev nodes into
the given service, and writes camera environment variables into <out_env>.
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

V4L_DIR="/dev/v4l/by-id"
ALL_VIDEO_DEVS=()
ALL_MEDIA_DEVS=()
ALL_SUBDEV_DEVS=()

touch "${OUT_ENV}"
>"${OUT_ENV}"

IFS=$'\n'
read -r -d '' -a ALL_VIDEO_DEVS < <(
    ls -1 /dev/video* 2>/dev/null |
        sort -V || true
    printf '\0'
)
read -r -d '' -a ALL_MEDIA_DEVS < <(
    ls -1 /dev/media* 2>/dev/null |
        sort -V || true
    printf '\0'
)
read -r -d '' -a ALL_SUBDEV_DEVS < <(
    ls -1 /dev/v4l-subdev* 2>/dev/null |
        sort -V || true
    printf '\0'
)

ALL_DEVS=()
for dev in "${ALL_VIDEO_DEVS[@]:-}" "${ALL_MEDIA_DEVS[@]:-}" "${ALL_SUBDEV_DEVS[@]:-}"; do
    [[ -n "$dev" ]] && ALL_DEVS+=("$dev")
done

if [[ -d "$V4L_DIR" ]]; then
    LINKS=()
    IFS=$'\n' LINKS=($(ls -1 "$V4L_DIR"/*-video-index0 2>/dev/null | sort -V || true))
else
    LINKS=()
fi

if [[ ${#LINKS[@]} -gt 0 ]]; then
    CAM_DEVS=()
    declare -A SEEN_DEVS=()

    for i in "${!LINKS[@]}"; do
        DEV="$(readlink -f "${LINKS[$i]}")"
        if [[ -n "${DEV}" && -z "${SEEN_DEVS["$DEV"]+x}" ]]; then
            CAM_DEVS+=("$DEV")
            SEEN_DEVS["$DEV"]=1
        fi
    done

    # Append any /dev/video* nodes that don't have by-id links.
    for DEV in "${ALL_VIDEO_DEVS[@]}"; do
        if [[ -n "${DEV}" && -z "${SEEN_DEVS["$DEV"]+x}" ]]; then
            CAM_DEVS+=("$DEV")
            SEEN_DEVS["$DEV"]=1
        fi
    done

    for i in "${!CAM_DEVS[@]}"; do
        echo "CAM${i}=${CAM_DEVS[$i]}" >>"$OUT_ENV"
    done
    echo "CAM_COUNT=${#CAM_DEVS[@]}" >>"$OUT_ENV"
    echo "Generated camera mapping for ${#CAM_DEVS[@]} camera(s)."
else
    for i in "${!ALL_VIDEO_DEVS[@]}"; do
        echo "CAM${i}=${ALL_VIDEO_DEVS[$i]}" >>"$OUT_ENV"
    done
    echo "CAM_COUNT=${#ALL_VIDEO_DEVS[@]}" >>"$OUT_ENV"
    echo "Generated camera mapping for ${#ALL_VIDEO_DEVS[@]} video node(s) (fallback)."
fi

{
    echo "services:"
    echo "  ${SERVICE_NAME}:"

    if [[ ${#ALL_DEVS[@]} -eq 0 ]]; then
        echo "    devices: []"
    else
        echo "    devices:"
        for DEV in "${ALL_DEVS[@]}"; do
            echo "      - ${DEV}:${DEV}"
        done
    fi
} >"${OUT_COMPOSE}"
