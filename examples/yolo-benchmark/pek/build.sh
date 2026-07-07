#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"

BUILD_TYPE="${1:-debug}"
ENABLE_TESTS="${2:-true}"
YOLO_BENCHMARK_BUILD_DIR="${YOLO_BENCHMARK_BUILD_DIR:-/tmp/yolo-benchmark-pek-build}"
TARGET_DIR="$REPO_ROOT/examples/bin"
TARGET="$TARGET_DIR/yolo-benchmark"

usage() {
    cat << EOF
Usage: $0 [debug|release] [true|false]

Rebuilds the PEK development tree, rebuilds the yolo-benchmark example, and copies
the resulting binary to examples/bin/yolo-benchmark.

Arguments:
  debug|release  Main PEK build type. Default: debug.
  true|false     Enable tests in the main PEK build. Default: true.

Environment:
  YOLO_BENCHMARK_BUILD_DIR  Meson build directory. Default: /tmp/yolo-benchmark-pek-build.
EOF
}

case "$BUILD_TYPE" in
    debug | release) ;;
    -h | --help)
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
    true | false) ;;
    *)
        echo "ENABLE_TESTS must be true or false, got: $ENABLE_TESTS" >&2
        usage >&2
        exit 2
        ;;
esac

"$REPO_ROOT/scripts/build-elements.sh" "$BUILD_TYPE" "$ENABLE_TESTS"

if [[ -d "$YOLO_BENCHMARK_BUILD_DIR/meson-private" ]]; then
    meson setup --reconfigure "$YOLO_BENCHMARK_BUILD_DIR" "$SCRIPT_DIR"
else
    meson setup "$YOLO_BENCHMARK_BUILD_DIR" "$SCRIPT_DIR"
fi

meson compile -C "$YOLO_BENCHMARK_BUILD_DIR"

mkdir -p "$TARGET_DIR"
cp "$YOLO_BENCHMARK_BUILD_DIR/yolo-benchmark" "$TARGET"
chmod +x "$TARGET"

echo "Installed $TARGET"
