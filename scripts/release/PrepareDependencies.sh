#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: PrepareDependencies.sh ARCH DESTINATION" >&2
    exit 2
fi

Architecture="$1"
Destination="$(realpath -m "$2")"
RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${ONNXRUNTIME_VERSION:?ONNXRUNTIME_VERSION is required}"

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

mkdir -p "$Destination"
# Do not reuse workspace or development-container dependencies: they are
# architecture/profile-specific and every dependency might not be available in one.
"$RepoRoot/scripts/private/install-onnxruntime.sh" \
    "$ONNXRUNTIME_VERSION" "$InstallArchitecture" "$Destination/onnxruntime"

mkdir -p "$Destination/notices"
cp "$Destination/onnxruntime/share/doc/onnxruntime/"* "$Destination/notices/"
