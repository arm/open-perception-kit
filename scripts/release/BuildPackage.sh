#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "Usage: BuildPackage.sh ARCH BUILD_ID OUTPUT_DIR NOTICES_DIR ONNX_ROOT" >&2
    exit 2
fi

Architecture="$1"
BuildId="$2"
OutputDir="$(realpath -m "$3")"
NoticesDir="$(realpath -m "$4")"
OnnxRoot="$(realpath -m "$5")"
RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

case "$Architecture" in
    x86_64 | aarch64) ;;
    *)
        echo "Unsupported architecture: $Architecture" >&2
        exit 2
        ;;
esac

[[ -d "$NoticesDir" && -d "$OnnxRoot" ]] || {
    echo "Release notice or ONNX Runtime input is missing" >&2
    exit 1
}
[[ -n "$(find "$NoticesDir" -maxdepth 1 -type f -print -quit)" ]] || {
    echo "Approved release notices are missing" >&2
    exit 1
}
[[ -z "$(find "$NoticesDir" -type l -print -quit)" ]] || {
    echo "Release notices must not contain symlinks" >&2
    exit 1
}

TemporaryRoot="$(mktemp -d)"
trap 'rm -rf "$TemporaryRoot"' EXIT
BuildRoot="$TemporaryRoot/build"
PackageName="pek-$BuildId-linux-$Architecture"
PackageRoot="$TemporaryRoot/$PackageName"
mkdir -p "$OutputDir" "$PackageRoot"

PEK_ONNXRUNTIME_ROOT="$OnnxRoot" \
    meson setup "$BuildRoot" "$RepoRoot/development" \
    --buildtype=release \
    --prefix=/ \
    --libdir=lib \
    -Dstrip=true \
    -Db_lto=true \
    -Doptimization=3 \
    -Dtests=false \
    -Drelease_package=true \
    -Dexecutorch=disabled \
    -Dhailort=disabled \
    -Dncnn=disabled
meson compile -C "$BuildRoot"
"$BuildRoot/meson-out/pek-config-check" --root "$RepoRoot"
DESTDIR="$PackageRoot" meson install -C "$BuildRoot" --skip-subprojects

mkdir -p "$PackageRoot/lib/pek" "$PackageRoot/share/pek/licenses"
OnnxLibrary="$OnnxRoot/lib/libonnxruntime.so.1.24.4"
[[ -f "$OnnxLibrary" ]] || {
    echo "Pinned ONNX Runtime 1.24.4 library is missing" >&2
    exit 1
}
[[ "$(readelf -dW "$OnnxLibrary" | sed -n 's/.*(SONAME).*\[\([^]]*\)\].*/\1/p')" == "libonnxruntime.so.1" ]] || {
    echo "Pinned ONNX Runtime has an unexpected SONAME" >&2
    exit 1
}
cp "$OnnxLibrary" "$PackageRoot/lib/pek/"
# Keep the upstream SONAME link: pek-onnx-ops.so needs libonnxruntime.so.1.
ln -s libonnxruntime.so.1.24.4 "$PackageRoot/lib/pek/libonnxruntime.so.1"

cp "$NoticesDir"/* "$PackageRoot/share/pek/licenses/"
python3 "$RepoRoot/scripts/release/ReleaseTool.py" stage-models \
    --repo-root "$RepoRoot" \
    --stage-root "$PackageRoot"
python3 "$RepoRoot/scripts/release/ReleaseTool.py" validate-package \
    --architecture "$Architecture" \
    --repo-root "$RepoRoot" \
    --package-root "$PackageRoot"

Archive="$OutputDir/$PackageName.tar.gz"
tar -C "$TemporaryRoot" -czf "$Archive" "$PackageName"
sha256sum "$Archive"
