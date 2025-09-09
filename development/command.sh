#!/usr/bin/env bash
set -euo pipefail

# ---- config ----
PROJECT_ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
ELEMENT="ampdummy"     # change if you rename the element

# ---- helpers ----
msg() { printf '[%s] %s\n' "$(basename "$0")" "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing tool: $1" >&2; exit 127; }; }

build() {
  need meson; need ninja

  msg "Build command in directory: $PROJECT_ROOT 🥷" 


  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "meson setup (debug)…"
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug
  else
    msg "meson configure (keeping existing build dir)…"
    meson configure "$BUILD_DIR" >/dev/null
  fi

  msg "compiling…"
  meson compile -C "$BUILD_DIR"
  msg "done → $BUILD_DIR"
}

clean() {
  if [[ -d "$BUILD_DIR" ]]; then
    msg "removing $BUILD_DIR…"
    rm -rf "$BUILD_DIR"
  else
    msg "nothing to clean"
  fi
}

test_run() {
  need gst-inspect-1.0; need gst-launch-1.0
  # Ensure it’s built
  [[ -d "$BUILD_DIR" ]] || build

  export GST_PLUGIN_PATH="$BUILD_DIR"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
  msg "gst-inspect-1.0 $ELEMENT"
  gst-inspect-1.0 "$ELEMENT" || { echo "Element '$ELEMENT' not found." >&2; exit 1; }

  msg "running test pipeline…"
  gst-launch-1.0 -q videotestsrc num-buffers=10 ! "$ELEMENT" ! fakesink
  msg "pipeline finished."
}

usage() {
  cat <<EOF
Usage: $(basename "$0") <command>

Commands:
  build    Configure (if needed) and compile with Meson/Ninja
  clean    Remove all build artifacts (delete '$BUILD_DIR')
  test     Build (if needed), export GST_PLUGIN_PATH, and run a demo pipeline

Examples:
  $(basename "$0") build
  $(basename "$0") clean
  $(basename "$0") test
EOF
}

# ---- entrypoint ----
cmd="${1:-}"
case "$cmd" in
  build) build ;;
  clean) clean ;;
  test)  test_run ;;
  ""|help|-h|--help) usage ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac