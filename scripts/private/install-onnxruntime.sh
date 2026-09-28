#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

descriptor="${1:?usage: install-onnxruntime.sh <build.json> [architecture] [destination]}"
architecture="${2:-$(uname -m)}"
destination="${3:-/opt/opk-deps/onnxruntime}"

case "${architecture}" in
    amd64 | x86_64)
        archive_architecture="x64"
        ;;
    arm64 | aarch64)
        archive_architecture="aarch64"
        ;;
    *)
        echo "Unsupported architecture for ONNX Runtime: ${architecture}" >&2
        exit 1
        ;;
esac

dependency_lock="$(python3 -c '
import json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    config = json.load(stream)
print(config["onnxruntime"], config["onnxruntime-sha256-" + sys.argv[2]])
' "$descriptor" "$archive_architecture")"
read -r version archive_sha256 <<< "$dependency_lock"

archive_directory="onnxruntime-linux-${archive_architecture}-${version}"
archive_url="https://github.com/microsoft/onnxruntime/releases/download/v${version}/${archive_directory}.tgz"
temporary_directory="$(mktemp -d)"
trap 'rm -rf "${temporary_directory}"' EXIT

archive="${temporary_directory}/${archive_directory}.tgz"
curl -fsSL -o "${archive}" "${archive_url}"
echo "${archive_sha256}  ${archive}" | sha256sum -c -
tar -xzf "${archive}" -C "${temporary_directory}"
rm -rf "${destination}"
mkdir -p "${destination}/share/doc/onnxruntime"
cp -r "${temporary_directory}/${archive_directory}/include" "${destination}/"
cp -r "${temporary_directory}/${archive_directory}/lib" "${destination}/"
cp "${temporary_directory}/${archive_directory}/LICENSE" \
    "${temporary_directory}/${archive_directory}/ThirdPartyNotices.txt" \
    "${destination}/share/doc/onnxruntime/"

python3 - "${destination}/provenance.json" "${archive_url}" "${archive_sha256}" \
    "${destination}/lib/libonnxruntime.so.${version}" << 'PYTHON'
import hashlib
import json
import sys
from pathlib import Path

receipt, url, archive_sha256, library = sys.argv[1:]
library = Path(library)
Path(receipt).write_text(json.dumps({
    "archive_url": url,
    "archive_sha256": archive_sha256,
    "library": library.name,
    "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PYTHON
