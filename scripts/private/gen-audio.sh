#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  gen-audio.sh <service_name> <out_compose_yaml> <out_env>

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
ALL_SND_DEVS=()
MIC_DEVS=()

touch "${OUT_ENV}"

if [[ -d "$SND_DIR" ]]; then
    IFS=$'\n'
    read -r -d '' -a ALL_SND_DEVS < <(
        find -L "$SND_DIR" \
            -maxdepth 1 \
            -type c \
            -print 2> /dev/null |
            sort -V || true
        printf '\0'
    )
    read -r -d '' -a MIC_DEVS < <(
        find -L "$SND_DIR" \
            -maxdepth 1 \
            -type c \
            -name 'pcmC*D*c' \
            -print 2> /dev/null |
            sort -V || true
        printf '\0'
    )
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
