#!/usr/bin/env bash

set -euo pipefail

SERVICE_NAME1="amp-dev-base"
SERVICE_NAME2="amp-dev-rich"
OUT_COMPOSE1="${2:-.devcontainer/docker-compose.devcont.video.yaml}"
OUT_COMPOSE2="${3:-.devcontainer/docker-compose.video.yaml}"
OUT_ENV="${3:-.devcontainer/cameras.env}"

V4L_DIR="/dev/v4l/by-id"

# --- Always create files (even if empty) ---
> "$OUT_ENV"

{
  echo "services:"
  echo "  ${SERVICE_NAME1}:"
  echo "    devices:"

} > "$OUT_COMPOSE1"

{
  echo "services:"
    echo "  ${SERVICE_NAME2}:"
    echo "    devices:"
} > "$OUT_COMPOSE2"

# --- If no Linux V4L, just exit cleanly ---
if [[ ! -d "$V4L_DIR" ]]; then
  echo "No V4L directory found. Generated empty video override."
  echo "CAM_COUNT=0" >> "$OUT_ENV"
  exit 0
fi

mapfile -t LINKS < <(ls -1 "$V4L_DIR"/*-video-index0 2>/dev/null | sort -V || true)
mapfile -t ALL_DEVS < <(ls -1 /dev/video* /dev/media* 2>/dev/null | sort -V || true)

if [[ ${#LINKS[@]} -eq 0 ]]; then
    echo "No cameras found. Generated empty video override."
    echo "CAM_COUNT=0" >> "$OUT_ENV"
    exit 0
fi

for i in "${!LINKS[@]}"; do
    DEV="$(readlink -f "${LINKS[$i]}")"
    echo "CAM${i}=${DEV}" >> "$OUT_ENV"
done

for i in "${!ALL_DEVS[@]}"; do
    DEV="${ALL_DEVS[$i]}"
    echo "      - ${DEV}:${DEV}" >> "$OUT_COMPOSE1"
done


for i in "${!ALL_DEVS[@]}"; do
    DEV="${ALL_DEVS[$i]}"
    echo "      - ${DEV}:${DEV}" >> "$OUT_COMPOSE2"
done

echo "CAM_COUNT=${#LINKS[@]}" >> "$OUT_ENV"

echo "Generated camera mapping for ${#LINKS[@]} camera(s)."
