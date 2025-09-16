#!/usr/bin/env bash

set -euo pipefail

# ---- include ----
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$SCRIPT_DIR/shtools.sh"

# ---- config ----
PROJECT_ROOT=/work/development
BUILD_DIR="$PROJECT_ROOT/build"

# ---- helpers ----
msg() { printf '[%s] %b\n' "$(basename "$0")" "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing tool: $1" >&2; exit 127; }; }

# ---- test ----

onnx() {

  msg_begin "Executing test with ONNX element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_endp "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  msg "Running test pipeline.."

  IP=$(getent ahostsv4 host.docker.internal | awk 'NR==1{print $1}')
  
  gst-launch-1.0 \
    filesrc location=/work/etc/videos/00.mp4 ! decodebin ! \
    videoconvert ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! \
    ampinfer model-path=/work/etc/models/yolov8n/yolov8n-fp32.onnx imgsz=640 ! \
    videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! \
    mpegtsmux ! \
    udpsink host="$IP" port=5000 sync=false async=false

  msg_end "Pipeline finished."
}


# ---- entrypoint ----

cmd="${1:-}"
case "$cmd" in
  onnx) onnx ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac