#!/usr/bin/env bash
set -euo pipefail

# ---- config ----
PROJECT_ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
ELEMENT="ampdummy"     # change if you rename the element

# ---- helpers ----
msg() { printf '[%s] %s\n' "$(basename "$0")" "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing tool: $1" >&2; exit 127; }; }

# ---- build ----

build() {
  need meson; need ninja
  msg "Build debug command in directory: $PROJECT_ROOT 🥷" 


  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "meson setup (debug)…"
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug
  else
    msg "meson configure (keeping existing build dir)…"
    meson configure "$BUILD_DIR" >/dev/null
  fi

  msg "compiling…"
  meson compile -C "$BUILD_DIR"
  msg "debug done → $BUILD_DIR"
}

build_release() {
  need meson; need ninja
  msg "Build release command in directory: $PROJECT_ROOT 🥷" 


  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "meson setup (release)…"
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" \
      --buildtype=release 
#      -Doptimization=3 \
#      -Ddebug=false \
#      -Dstrip=true \
#      -Db_lto=true
  else
    msg "meson configure (keeping existing build dir)…"
    meson configure "$BUILD_DIR" >/dev/null
  fi

  msg "compiling…"
  meson compile -C "$BUILD_DIR"
  msg "reelase done → $BUILD_DIR"
}

# ---- clean ----

clean() {
  if [[ -d "$BUILD_DIR" ]]; then
    msg "removing $BUILD_DIR…"
    rm -rf "$BUILD_DIR"
  else
    msg "nothing to clean"
  fi
}

# ---- test ----

test_run() {
  need gst-inspect-1.0; need gst-launch-1.0
  # Ensure it’s built
  [[ -d "$BUILD_DIR" ]] || build

  export GST_PLUGIN_PATH="$BUILD_DIR"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
  msg "gst-inspect-1.0 $ELEMENT"
  gst-inspect-1.0 "$ELEMENT" || { echo "Element '$ELEMENT' not found." >&2; exit 1; }

  msg "running test pipeline…"
# gst-launch-1.0 videotestsrc ! ampdummy ! waylandsink
  #gst-launch-1.0 -q videotestsrc num-buffers=200 ! "$ELEMENT" ! videoconvert ! ximagesink sync=false
# waylandsink

#  gst-launch-1.0 filesrc location=/work/videos/00.mp4 ! decodebin ! \
#    videoconvert ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! \
#    ampinfer model-path=/work/etc/models/yolov8n/yolov8n-fp32.onnx imgsz=640 ! \
#    videoconvert !  ximagesink sync=false

  gst-launch-1.0 \
    filesrc location=/work/etc/videos/00.mp4 ! decodebin ! \
    videoconvert ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! \
    ampinfer model-path=/work/etc/models/yolov8n/yolov8n-fp32.onnx imgsz=640 ! \
    videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! \
    mpegtsmux ! \
    udpsink host=$(getent hosts host.docker.internal | awk '{print $1}') port=5000

  msg "pipeline finished."
}

# ---- help ----

usage() {
  cat <<EOF
Usage: $(basename "$0") <command>

Commands:
  build ➡️   Configure (if needed) and compile debug with Meson/Ninja
  rbuild ➡️   Configure (if needed) and compile release with Meson/Ninja
  clean ➡️   Remove all build artifacts (delete '$BUILD_DIR')
  test  ➡️   Build (if needed), export GST_PLUGIN_PATH, and run a demo pipeline

Examples:
  $(basename "$0") build
  $(basename "$0") rbuild
  $(basename "$0") clean
  $(basename "$0") test
EOF
}

# ---- entrypoint ----

cmd="${1:-}"
case "$cmd" in
  build) build ;;
  rbuild) build_release ;;
  clean) clean ;;
  test)  test_run ;;
  ""|help) usage ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac