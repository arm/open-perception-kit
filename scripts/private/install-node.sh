#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

descriptor="${1:?usage: install-node.sh <build.json>}"
version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["node"])' "$descriptor")"
readonly DOWNLOAD_RETRIES=3
readonly RETRY_DELAY_SECONDS=2

case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)
        architecture=x64
        ;;
    Linux-aarch64)
        architecture=arm64
        ;;
    *)
        echo "Node installation supports Linux x86_64 and aarch64 only" >&2
        exit 1
        ;;
esac

if command -v node > /dev/null && [[ "$(node --version)" == "v${version}" ]] && command -v npm > /dev/null; then
    exit 0
fi

archive="node-v${version}-linux-${architecture}.tar.gz"
download_url="https://nodejs.org/dist/v${version}"
temporary_directory="$(mktemp -d)"
trap 'rm -rf "$temporary_directory"' EXIT

for filename in "$archive" SHASUMS256.txt; do
    curl --proto '=https' --proto-redir '=https' --location --fail \
        --retry "$DOWNLOAD_RETRIES" --retry-all-errors --retry-delay "$RETRY_DELAY_SECONDS" \
        --output "$temporary_directory/$filename" "$download_url/$filename"
done
checksum="$(awk -v archive="$archive" '$2 == archive { print $1 }' "$temporary_directory/SHASUMS256.txt")"
printf '%s  %s\n' "$checksum" "$temporary_directory/$archive" | sha256sum --check --strict
tar -xzf "$temporary_directory/$archive" --strip-components=1 --no-same-owner -C /usr/local
hash -r
test "$(node --version)" = "v${version}"
npm --version
