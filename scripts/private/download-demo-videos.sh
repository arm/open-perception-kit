#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Downloads checksum-locked demo videos from the OPK public Box folder.
#
# Usage: ./scripts/private/download-demo-videos.sh [--check]
################################################################

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
videos_dir="$(cd "${script_dir}/../.." && pwd)/data/videos"
manifest="${script_dir}/demo-videos.manifest"
BOX_SHARED_TOKEN="yk3v2zpd10s9skbmlrv1lbn82hinga5u"

sha256=(sha256sum)
command -v sha256sum > /dev/null 2>&1 || sha256=(shasum -a 256)

checksums() {
    awk '$1 !~ /^#/ { print $1 "  " $3 }' "$manifest"
}

check_all() {
    [[ -d "$videos_dir" ]] || return 1
    checksums | (cd "$videos_dir" && "${sha256[@]}" --check --strict --quiet)
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
while read -r expected file_id filename; do
    [[ "$expected" == \#* ]] && continue
    destination="${videos_dir}/${filename}"
    if printf '%s  %s\n' "$expected" "$destination" | "${sha256[@]}" --check --strict --quiet 2> /dev/null; then
        continue
    fi

    temporary="${destination}.part"
    rm -f "$temporary"
    url="https://arm.app.box.com/index.php?rm=box_download_shared_file&shared_link=${BOX_SHARED_TOKEN}&shared_name=${BOX_SHARED_TOKEN}&file_id=${file_id}"
    if ! curl -fsSL --retry 3 --retry-delay 2 -o "$temporary" "$url" ||
        ! printf '%s  %s\n' "$expected" "$temporary" | "${sha256[@]}" --check --strict --quiet; then
        rm -f "$temporary"
        exit 1
    fi
    mv -f "$temporary" "$destination"
done < "$manifest"

checksums > "${videos_dir}/SHA256SUMS"
check_all
