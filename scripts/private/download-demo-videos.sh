#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Downloads checksum-locked demo video assets from the PEK public Box folder
# into data/videos/. Existing valid files are reused.
#
# Usage: ./scripts/private/download-demo-videos.sh [--check]
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VIDEOS_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)/data/videos"
LOCK_FILE="${SCRIPT_DIR}/demo-videos.manifest"
CHECKSUMS_FILE="${VIDEOS_DIR}/SHA256SUMS"
BOX_SHARED_TOKEN="yk3v2zpd10s9skbmlrv1lbn82hinga5u"

mode="download"
case "${1:-}" in
    "") ;;
    --check) mode="check" ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac

box_download_url() {
    local file_id="$1"
    echo "https://arm.app.box.com/index.php?rm=box_download_shared_file&shared_link=${BOX_SHARED_TOKEN}&shared_name=${BOX_SHARED_TOKEN}&file_id=${file_id}"
}

sha256_file() {
    local path="$1"

    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$path" | awk '{print $1}'
    elif command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$path" | awk '{print $1}'
    else
        echo "ERROR: sha256sum or shasum is required" >&2
        return 2
    fi
}

verify_file() {
    local path="$1"
    local expected="$2"
    [[ -f "$path" ]] && [[ "$(sha256_file "$path")" == "$expected" ]]
}

validate_entry() {
    local expected="$1"
    local file_id="$2"
    local filename="$3"

    if [[ ! "$expected" =~ ^[0-9a-f]{64}$ ]] ||
        [[ ! "$file_id" =~ ^f_[0-9]+$ ]] ||
        [[ ! "$filename" =~ ^GettyImages-[0-9]+\.mov$ ]]; then
        echo "ERROR: invalid demo video lock entry: $expected $file_id $filename" >&2
        return 2
    fi
}

checksum_tmp=""
temporary=""
cleanup() {
    [[ -z "$checksum_tmp" ]] || rm -f "$checksum_tmp"
    [[ -z "$temporary" ]] || rm -f "$temporary"
}
trap cleanup EXIT

if [[ "$mode" == "download" ]]; then
    mkdir -p "$VIDEOS_DIR"
    checksum_tmp="$(mktemp "${VIDEOS_DIR}/.SHA256SUMS.XXXXXX")"
    echo "Downloading demo videos to: $VIDEOS_DIR"
else
    echo "Checking demo videos in: $VIDEOS_DIR"
fi
echo

count=0
failed=0
while read -r expected file_id filename; do
    if [[ -z "$expected" || "$expected" == \#* ]]; then
        continue
    fi
    validate_entry "$expected" "$file_id" "$filename"
    count=$((count + 1))
    destination="${VIDEOS_DIR}/${filename}"

    if verify_file "$destination" "$expected"; then
        echo "OK:   $filename"
    else
        if [[ "$mode" == "check" ]]; then
            echo "FAIL: $filename is missing or has the wrong SHA-256" >&2
            failed=$((failed + 1))
            continue
        fi

        temporary="${destination}.part"
        rm -f "$temporary"
        echo "GET:  $filename"
        if curl -fsSL --retry 3 --retry-delay 2 \
            -o "$temporary" "$(box_download_url "$file_id")" &&
            verify_file "$temporary" "$expected"; then
            mv -f "$temporary" "$destination"
            temporary=""
        else
            echo "FAIL: $filename download or SHA-256 verification failed" >&2
            rm -f "$temporary"
            failed=$((failed + 1))
            continue
        fi
    fi

    if [[ "$mode" == "download" ]]; then
        printf '%s  %s\n' "$expected" "$filename" >> "$checksum_tmp"
    fi
done < "$LOCK_FILE"

echo
if [[ "$count" -eq 0 || "$failed" -gt 0 ]]; then
    echo "Demo video verification failed: checked=$count failed=$failed" >&2
    exit 1
fi

if [[ "$mode" == "download" ]]; then
    mv -f "$checksum_tmp" "$CHECKSUMS_FILE"
    checksum_tmp=""
    chmod 0444 "$CHECKSUMS_FILE"
fi
echo "Verified $count demo videos."
