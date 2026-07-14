#!/usr/bin/env bash

set -euo pipefail

version="${1:?ONNX Runtime version is required}"
architecture="${2:-$(uname -m)}"
destination="${3:-/opt/pek-deps/onnxruntime}"

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

archive_directory="onnxruntime-linux-${archive_architecture}-${version}"
archive_url="https://github.com/microsoft/onnxruntime/releases/download/v${version}/${archive_directory}.tgz"
temporary_directory="$(mktemp -d)"
trap 'rm -rf "${temporary_directory}"' EXIT

curl -fsSL "${archive_url}" | tar -xzf - -C "${temporary_directory}"
rm -rf "${destination}"
mkdir -p "${destination}"
cp -r "${temporary_directory}/${archive_directory}/include" "${destination}/"
cp -r "${temporary_directory}/${archive_directory}/lib" "${destination}/"
