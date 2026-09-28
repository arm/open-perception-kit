#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

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
    "${temporary_directory}/${archive_directory}/GIT_COMMIT_ID" \
    "${temporary_directory}/${archive_directory}/VERSION_NUMBER" \
    "${destination}/share/doc/onnxruntime/"
