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

descriptor="${1:?usage: install-onnxruntime.sh <build.json> [architecture] [destination]}"
architecture="${2:-$(uname -m)}"
destination="${3:-/opt/opk-deps/onnxruntime}"
# NuGet remains the default while the GitHub release assets are unavailable.
source="${OPK_ONNXRUNTIME_SOURCE:-nuget}"

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

case "${source}" in
    github) checksum_suffix="${archive_architecture}" ;;
    nuget) checksum_suffix="nuget" ;;
    *)
        echo "Unsupported ONNX Runtime source: ${source}" >&2
        exit 1
        ;;
esac

dependency_lock="$(python3 -c '
import json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    config = json.load(stream)
print(config["onnxruntime"], config["onnxruntime-sha256-" + sys.argv[2]])
' "$descriptor" "$checksum_suffix")"
read -r version archive_sha256 <<< "$dependency_lock"

archive_directory="onnxruntime-linux-${archive_architecture}-${version}"
archive_url="https://github.com/microsoft/onnxruntime/releases/download/v${version}/${archive_directory}.tgz"
if [[ "${source}" == nuget ]]; then
    archive_url="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${version}/microsoft.ml.onnxruntime.${version}.nupkg"
fi
temporary_directory="$(mktemp -d)"
trap 'rm -rf "${temporary_directory}"' EXIT

archive="${temporary_directory}/${archive_url##*/}"
curl -fsSL --retry 3 --retry-all-errors --retry-delay 2 -o "${archive}" "${archive_url}"
echo "${archive_sha256}  ${archive}" | sha256sum -c -
install_directory="${temporary_directory}/install"
mkdir -p "${install_directory}"/{include,lib,share/doc/onnxruntime}
if [[ "${source}" == nuget ]]; then
    unzip -qj "${archive}" 'build/native/include/*' -d "${install_directory}/include"
    unzip -qj "${archive}" "runtimes/linux-${archive_architecture/aarch64/arm64}/native/*.so" -d "${install_directory}/lib"
    unzip -qj "${archive}" LICENSE ThirdPartyNotices.txt -d "${install_directory}/share/doc/onnxruntime"
    unzip -p "${archive}" Microsoft.ML.OnnxRuntime.nuspec > "${temporary_directory}/Microsoft.ML.OnnxRuntime.nuspec"
    mv "${install_directory}/lib/libonnxruntime.so" "${install_directory}/lib/libonnxruntime.so.${version}"
    ln -s "libonnxruntime.so.${version}" "${install_directory}/lib/libonnxruntime.so.1"
    ln -s libonnxruntime.so.1 "${install_directory}/lib/libonnxruntime.so"
else
    tar -xzf "${archive}" -C "${temporary_directory}"
    cp -r "${temporary_directory}/${archive_directory}"/{include,lib} "${install_directory}/"
    cp "${temporary_directory}/${archive_directory}"/{LICENSE,ThirdPartyNotices.txt,GIT_COMMIT_ID,VERSION_NUMBER} \
        "${install_directory}/share/doc/onnxruntime/"
fi

python3 - "${install_directory}/provenance.json" "${archive_url}" "${archive_sha256}" \
    "${install_directory}/lib/libonnxruntime.so.${version}" \
    "${temporary_directory}/Microsoft.ML.OnnxRuntime.nuspec" "${version}" << 'PYTHON'
import hashlib
import json
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

receipt, url, archive_sha256, library, manifest, version = sys.argv[1:]
if Path(manifest).is_file():
    metadata = ET.parse(manifest)
    if metadata.findtext(".//{*}version") != version:
        raise ValueError("ONNX Runtime package version does not match the lock")
    legal = Path(receipt).parent / "share/doc/onnxruntime"
    (legal / "GIT_COMMIT_ID").write_text(metadata.find(".//{*}repository").attrib["commit"] + "\n")
    (legal / "VERSION_NUMBER").write_text(version + "\n")
library = Path(library)
Path(receipt).write_text(json.dumps({
    "archive_url": url,
    "archive_sha256": archive_sha256,
    "library": library.name,
    "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PYTHON

mkdir -p "$(dirname "${destination}")"
rm -rf "${destination}"
mv "${install_directory}" "${destination}"
