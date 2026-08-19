#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "Usage: BuildPackage.sh ARCH BUILD_ID OUTPUT_DIR RELEASE_DEPENDENCIES_DIR PERCEPTION_SDK_INPUT_DIR" >&2
    exit 2
fi

Architecture="$1"
BuildId="$2"
OutputDir="$(realpath -m "$3")"
ReleaseDependenciesDir="$(realpath -m "$4")"
PerceptionSdkInputDir="$(realpath -m "$5")"
RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

case "$Architecture" in
    x86_64 | aarch64) ;;
    *)
        echo "Unsupported architecture: $Architecture" >&2
        exit 2
        ;;
esac

[[ -d "$ReleaseDependenciesDir" && ! -L "$ReleaseDependenciesDir" ]] || {
    echo "Release dependencies directory is missing or invalid" >&2
    exit 1
}
OnnxRoot="$ReleaseDependenciesDir/onnxruntime"
ExecutorchRoot="$ReleaseDependenciesDir/executorch"
LibtorchRoot="$ReleaseDependenciesDir/libtorch"
LegalDocumentationRoot="$ReleaseDependenciesDir/legal-documentation"
for RequiredInput in "$OnnxRoot" "$ExecutorchRoot" "$LibtorchRoot" "$LegalDocumentationRoot"; do
    [[ -d "$RequiredInput" && ! -L "$RequiredInput" ]] || {
        echo "Prepared dependency input is missing or invalid: $RequiredInput" >&2
        exit 1
    }
done
python3 "$RepoRoot/scripts/release/ReleaseTool.py" validate-perception-sdk \
    --repo-root "$RepoRoot" \
    --perception-sdk-root "$PerceptionSdkInputDir"
[[ -n "$(find "$LegalDocumentationRoot" -type f -print -quit)" ]] || {
    echo "Approved release legal documentation is missing" >&2
    exit 1
}
[[ -z "$(find "$LegalDocumentationRoot" -type l -print -quit)" ]] || {
    echo "Release legal documentation must not contain symlinks" >&2
    exit 1
}

TemporaryRoot="$(mktemp -d)"
trap 'rm -rf "$TemporaryRoot"' EXIT
BuildRoot="$TemporaryRoot/build"
PackageName="pek-$BuildId-linux-$Architecture"
PackageRoot="$TemporaryRoot/$PackageName"
mkdir -p "$OutputDir" "$PackageRoot"

PEK_ONNXRUNTIME_ROOT="$OnnxRoot" \
    PEK_EXECUTORCH_ROOT="$ExecutorchRoot" \
    PEK_LIBTORCH_ROOT="$LibtorchRoot" \
    meson setup "$BuildRoot" "$RepoRoot/development" \
    --buildtype=release \
    --prefix=/ \
    --libdir=lib \
    -Dstrip=true \
    -Db_lto=true \
    -Doptimization=3 \
    -Dtests=false \
    -Drelease_package=true \
    -Dexecutorch=enabled \
    -Dhailort=disabled \
    -Dncnn=disabled
meson compile -C "$BuildRoot"
"$BuildRoot/config-validator/pek-config-check" --root "$RepoRoot"
DESTDIR="$PackageRoot" meson install -C "$BuildRoot" --skip-subprojects

mkdir -p \
    "$PackageRoot/lib/pek" \
    "$PackageRoot/share/pek/licenses" \
    "$PackageRoot/share/pek/perception-sdk"
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

cp -a "$LegalDocumentationRoot/." "$PackageRoot/share/pek/licenses/"
cp -a "$PerceptionSdkInputDir/." "$PackageRoot/share/pek/perception-sdk/"
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
