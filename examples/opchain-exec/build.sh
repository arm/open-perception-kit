#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"

BUILD_TYPE="${1:-debug}"
ENABLE_TESTS="${2:-true}"
OPCHAIN_EXEC_BUILD_DIR="${OPCHAIN_EXEC_BUILD_DIR:-/tmp/opchain-exec-build}"
TARGET_DIR="$REPO_ROOT/examples/bin"
TARGET="$TARGET_DIR/opchain-exec"

usage() {
    cat << EOF
Usage: $0 [debug|release] [true|false]

Rebuilds the PEK development tree, rebuilds the opchain-exec example, and copies the
resulting binary to examples/bin/opchain-exec.

Arguments:
  debug|release  Main PEK build type. Default: debug.
  true|false     Enable tests in the main PEK build. Default: true.

Environment:
  OPCHAIN_EXEC_BUILD_DIR  Meson build directory for opchain-exec. Default: /tmp/opchain-exec-build.
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

if [[ -d "$OPCHAIN_EXEC_BUILD_DIR/meson-private" ]]; then
    meson setup --reconfigure "$OPCHAIN_EXEC_BUILD_DIR" "$SCRIPT_DIR"
else
    meson setup "$OPCHAIN_EXEC_BUILD_DIR" "$SCRIPT_DIR"
fi

meson compile -C "$OPCHAIN_EXEC_BUILD_DIR"

mkdir -p "$TARGET_DIR"
cp "$OPCHAIN_EXEC_BUILD_DIR/opchain-exec" "$TARGET"
chmod +x "$TARGET"

echo "Installed $TARGET"
