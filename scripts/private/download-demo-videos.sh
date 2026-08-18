#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Downloads demo video assets from the PEK public Box folder
# into data/videos/. Skips files that already exist.
#
# Usage: ./scripts/private/download-demo-videos.sh
################################################################

set -euo pipefail

VIDEOS_DIR="$(cd "$(dirname "$0")/../.." && pwd)/data/videos"
BOX_SHARED_TOKEN="yk3v2zpd10s9skbmlrv1lbn82hinga5u"

# Format: "filename:file_id"
declare -a VIDEO_FILES=(
    "GettyImages-1129703310.mov:f_2208931731793"
    "GettyImages-1140581459.mov:f_2219383132316"
    "GettyImages-1298072556.mov:f_2219375639006"
    "GettyImages-1465682313.mov:f_2219385842023"
    "GettyImages-2165518864.mov:f_2208923252936"
    "GettyImages-2174094355.mov:f_2219380024458"
    "GettyImages-2205397623.mov:f_2219387317230"
    "GettyImages-2220092613.mov:f_2220528008798"
    "GettyImages-2222093886.mov:f_2220530132752"
    "GettyImages-2259414639.mov:f_2219376743441"
    "GettyImages-2264926445.mov:f_2220535341493"
)

box_download_url() {
    local file_id="$1"
    echo "https://arm.app.box.com/index.php?rm=box_download_shared_file&shared_link=${BOX_SHARED_TOKEN}&shared_name=${BOX_SHARED_TOKEN}&file_id=${file_id}"
}

mkdir -p "$VIDEOS_DIR"

echo "Downloading demo videos to: $VIDEOS_DIR"
echo

skipped=0
downloaded=0
failed=0

for entry in "${VIDEO_FILES[@]}"; do
    filename="${entry%%:*}"
    file_id="${entry##*:}"
    dest="$VIDEOS_DIR/$filename"

    if [[ -f "$dest" ]]; then
        echo "SKIP: $filename (already exists)"
        skipped=$((skipped + 1))
        continue
    fi

    echo "Downloading: $filename ..."
    url="$(box_download_url "$file_id")"

    if curl -fsSL --retry 3 --retry-delay 2 -o "$dest" "$url"; then
        echo "OK:   $filename"
        downloaded=$((downloaded + 1))
    else
        echo "ERROR: Failed to download $filename"
        rm -f "$dest"
        failed=$((failed + 1))
    fi
done

echo
echo "Done. downloaded=$downloaded skipped=$skipped failed=$failed"

if [[ "$failed" -gt 0 ]]; then
    exit 1
fi
