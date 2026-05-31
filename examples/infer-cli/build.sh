#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"

BUILD_TYPE="${1:-debug}"
ENABLE_TESTS="${2:-true}"
INFER_CLI_BUILD_DIR="${INFER_CLI_BUILD_DIR:-/tmp/infer-cli-build}"
TARGET_DIR="$REPO_ROOT/examples/bin"
TARGET="$TARGET_DIR/infer-cli"

usage() {
    cat << EOF
Usage: $0 [debug|release] [true|false]

Rebuilds the PEK development tree, rebuilds the infer-cli example, and copies the
resulting binary to examples/bin/infer-cli.

Arguments:
  debug|release  Main PEK build type. Default: debug.
  true|false     Enable tests in the main PEK build. Default: true.

Environment:
  INFER_CLI_BUILD_DIR  Meson build directory for infer-cli. Default: /tmp/infer-cli-build.
EOF
}

case "$BUILD_TYPE" in
    debug|release)
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "Unsupported build type: $BUILD_TYPE" >&2
        usage >&2
        exit 2
        ;;
esac

case "$ENABLE_TESTS" in
    true|false)
        ;;
    *)
        echo "ENABLE_TESTS must be true or false, got: $ENABLE_TESTS" >&2
        usage >&2
        exit 2
        ;;
esac

"$REPO_ROOT/scripts/build-elements.sh" "$BUILD_TYPE" "$ENABLE_TESTS"

if [[ -d "$INFER_CLI_BUILD_DIR/meson-private" ]]; then
    meson setup --reconfigure "$INFER_CLI_BUILD_DIR" "$SCRIPT_DIR"
else
    meson setup "$INFER_CLI_BUILD_DIR" "$SCRIPT_DIR"
fi

meson compile -C "$INFER_CLI_BUILD_DIR"

mkdir -p "$TARGET_DIR"
cp "$INFER_CLI_BUILD_DIR/infer-cli" "$TARGET"
chmod +x "$TARGET"

echo "Installed $TARGET"
