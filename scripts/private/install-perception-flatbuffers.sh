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
curl --retry 3 --retry-all-errors --retry-delay 2 -fsSLo "$archive" "$url"
printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum -c -
tar -xzf "$archive" -C "$tmp_dir"
cmake -S "$tmp_dir/flatbuffers-${version}" -B "$tmp_dir/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFLATBUFFERS_BUILD_TESTS=OFF \
    -DFLATBUFFERS_INSTALL=ON
cmake --build "$tmp_dir/build" --target install --parallel "$(nproc)"
ldconfig
