#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: PrepareDependencies.sh ARCH RELEASE_DEPENDENCIES_DIR" >&2
    exit 2
fi

Architecture="$1"
ReleaseDependenciesDir="$(realpath -m "$2")"
RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${ONNXRUNTIME_VERSION:?ONNXRUNTIME_VERSION is required}"
: "${EXECUTORCH_VERSION:?EXECUTORCH_VERSION is required}"
: "${EXECUTORCH_DEB_REVISION:?EXECUTORCH_DEB_REVISION is required}"
: "${EXECUTORCH_ARTIFACTORY_USERNAME:?EXECUTORCH_ARTIFACTORY_USERNAME is required}"
: "${EXECUTORCH_ARTIFACTORY_PASSWORD:?EXECUTORCH_ARTIFACTORY_PASSWORD is required}"

case "$Architecture" in
    x86_64)
        InstallArchitecture=amd64
        ;;
    aarch64)
        InstallArchitecture=arm64
        ;;
    *)
        echo "Unsupported architecture: $Architecture" >&2
        exit 2
        ;;
esac

mkdir -p "$ReleaseDependenciesDir"

# Do not reuse workspace or development-container dependencies: they are
# architecture/profile-specific and every dependency might not be available in one.
"$RepoRoot/scripts/private/install-onnxruntime.sh" \
    "$ONNXRUNTIME_VERSION" "$InstallArchitecture" "$ReleaseDependenciesDir/onnxruntime"

EXECUTORCH_ARTIFACTORY_SERVER=https://artifactory.arm.com:443 \
    EXECUTORCH_ARTIFACTORY_REPOSITORY=ai-expkits-internal.opk-deb \
    EXECUTORCH_ARTIFACTORY_DISTRIBUTION=trixie \
    EXECUTORCH_ARTIFACTORY_COMPONENT=main \
    "$RepoRoot/scripts/private/executorch/install-executorch-deb.sh"

NativeArchitecture="$(dpkg --print-architecture)"
InstalledPackage="$(dpkg-query -W -f='${Package}' libexecutorch-dev)"
InstalledVersion="$(dpkg-query -W -f='${Version}' libexecutorch-dev)"
InstalledArchitecture="$(dpkg-query -W -f='${Architecture}' libexecutorch-dev)"
[[ "$InstalledPackage" == "libexecutorch-dev" ]] || {
    echo "Unexpected installed package: $InstalledPackage" >&2
    exit 1
}
[[ "$InstalledVersion" == "$EXECUTORCH_VERSION-$EXECUTORCH_DEB_REVISION" ]] || {
    echo "Unexpected installed ExecuTorch version: $InstalledVersion" >&2
    exit 1
}
[[ "$InstalledArchitecture" == "$NativeArchitecture" ]] || {
    echo "ExecuTorch package architecture $InstalledArchitecture is not native $NativeArchitecture" >&2
    exit 1
}

DocumentationRoot=/opt/pek-deps/executorch-legal-documentation
[[ -d "$DocumentationRoot" && ! -L "$DocumentationRoot" ]] || {
    echo "ExecuTorch package documentation directory is missing" >&2
    exit 1
}
[[ -n "$(find "$DocumentationRoot" -type f -print -quit)" ]] || {
    echo "ExecuTorch package documentation directory is empty" >&2
    exit 1
}
[[ -z "$(find "$DocumentationRoot" -type l -print -quit)" ]] || {
    echo "ExecuTorch package documentation must not contain symlinks" >&2
    exit 1
}

cp -a /opt/pek-deps/executorch "$ReleaseDependenciesDir/executorch"
cp -a /opt/pek-deps/libtorch "$ReleaseDependenciesDir/libtorch"
mkdir -p "$ReleaseDependenciesDir/legal-documentation/libexecutorch-dev"
cp -a "$ReleaseDependenciesDir/onnxruntime/share/doc/onnxruntime/." \
    "$ReleaseDependenciesDir/legal-documentation/"
cp -a "$DocumentationRoot/." \
    "$ReleaseDependenciesDir/legal-documentation/libexecutorch-dev/"
