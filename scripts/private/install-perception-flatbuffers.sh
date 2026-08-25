#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

descriptor="${1:?usage: install-perception-flatbuffers.sh <sdk.json>}"
readarray -t lock < <(python3 -c \
    'import json, sys; data=json.load(open(sys.argv[1], encoding="utf-8"))["flatbuffers"]; print(data["version"]); print(data["source_archive"]["filename"]); print(data["source_archive"]["url"]); print(data["source_archive"]["sha256"])' \
    "$descriptor")
version="${lock[0]}"
filename="${lock[1]}"
url="${lock[2]}"
expected_sha256="${lock[3]}"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

archive="$tmp_dir/$filename"
curl -fsSLo "$archive" "$url"
printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum -c -
tar -xzf "$archive" -C "$tmp_dir"
cmake -S "$tmp_dir/flatbuffers-${version}" -B "$tmp_dir/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFLATBUFFERS_BUILD_TESTS=OFF \
    -DFLATBUFFERS_INSTALL=ON
cmake --build "$tmp_dir/build" --target install --parallel "$(nproc)"
ldconfig
