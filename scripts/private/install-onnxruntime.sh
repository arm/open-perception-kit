#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -lt 1 || -z "$1" ]]; then
    echo "ONNX Runtime version is required" >&2
    exit 2
fi

version="$1"
architecture="${2:-$(uname -m)}"
destination="${3:-/opt/pek-deps/onnxruntime}"

case "${architecture}" in
    amd64 | x86_64)
        archive_architecture="x64"
        archive_sha256="3a211fbea252c1e66290658f1b735b772056149f28321e71c308942cdb54b747"
        ;;
    arm64 | aarch64)
        archive_architecture="aarch64"
        archive_sha256="866109a9248d057671a039b9d725be4bd86888e3754140e6701ec621be9d4d7e"
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

[[ "${version}" == "1.24.4" ]] || {
    echo "Unsupported ONNX Runtime version without a checked-in checksum: ${version}" >&2
    exit 1
}
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
