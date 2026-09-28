#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################
# Downloads checksum-locked demo videos from the Arm Multimedia
# Hugging Face bucket.
#
# Usage: ./scripts/private/download-demo-videos.sh [--check]
################################################################

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
videos_dir="$(cd "${script_dir}/../.." && pwd)/data/videos"
manifest="${script_dir}/demo-videos.manifest"
bucket_url="https://huggingface.co/buckets/Arm/Multimedia/resolve/sample-videos"

sha256=(sha256sum)
command -v sha256sum > /dev/null 2>&1 || sha256=(shasum -a 256)

checksums() {
    awk '$1 !~ /^#/ { print $1 "  " $2 }' "$manifest"
}

checksum_matches() {
    local expected="$1"
    local file="$2"
    [[ -f "$file" ]] &&
        printf '%s  %s\n' "$expected" "$file" |
            "${sha256[@]}" --check --strict --quiet > /dev/null 2>&1
}

check_all() {
    local missing=0
    local mismatched=0
    local errors
    local file
    while read -r expected filename; do
        [[ "$expected" == \#* ]] && continue
        file="${videos_dir}/${filename}"
        if [[ ! -f "$file" ]]; then
            echo "MISSING: ${filename}"
            ((missing += 1))
        elif ! checksum_matches "$expected" "$file"; then
            echo "CHECKSUM MISMATCH: ${filename}"
            ((mismatched += 1))
        fi
    done < "$manifest"

    errors=$((missing + mismatched))
    if ((errors > 0)); then
        echo "ERROR: verification failed for ${errors} files (missing: ${missing}, checksum mismatches: ${mismatched})" >&2
        return 1
    fi
}

case "${1:-}" in
    --check)
        check_all
        exit
        ;;
    "") ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac

mkdir -p "$videos_dir"
while read -r expected filename; do
    [[ "$expected" == \#* ]] && continue
    destination="${videos_dir}/${filename}"
    if checksum_matches "$expected" "$destination"; then
        continue
    fi

    temporary="${destination}.part"
    rm -f "$temporary"
    url="${bucket_url}/${filename}"
    if ! curl -fsSL --retry 3 --retry-delay 2 -o "$temporary" "$url" ||
        ! checksum_matches "$expected" "$temporary"; then
        echo "ERROR: failed to download or verify ${filename}" >&2
        rm -f "$temporary"
        exit 1
    fi
    mv -f "$temporary" "$destination"
done < "$manifest"

checksums > "${videos_dir}/SHA256SUMS"
check_all
