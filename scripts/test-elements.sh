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

onnx_rgb() {

  msg_begin "Executing test with ONNX element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR/meson-out"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  msg "Running test pipeline.."


gst-launch-1.0 \
  filesrc location=/work/etc/images/katana.jpg ! \
  jpegdec ! \
  imagefreeze ! \
  videoconvert ! \
      ampinfer opchain-path=/work/etc/models/yolo/opchain.json active=true ! \
      ampinfer opchain-path=/work/etc/models/ultraface/opchain.json active=true ! \
      ampinfer opchain-path=/work/etc/models/personclassification/opchain.json active=true ! \
      ampinfer opchain-path=/work/etc/models/gazedetection/opchain.json active=true ! \
      textoverlay name=overlay valignment=top halignment=center font-desc="Sans, 14" ! \
  videoconvert ! \
      ampperformance show-all-metrics=true x-offset=20 y-offset=20 font-size=18 alpha=0.9 update-interval=1 ! \
  videoconvert ! \
      ampsink name=sink

  msg_end "Pipeline finished."
}

onnx() {

  msg_begin "Executing test with ONNX element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  msg "Running test pipeline.."

  # IP=$(getent ahostsv4 host.docker.internal | awk 'NR==1{print $1}')
  

#  gst-launch-1.0 \
#    filesrc location=/work/etc/videos/00.mp4 ! decodebin name=dec \
#    dec. ! queue ! video/x-raw ! videoconvert ! \
#      ampinfer model-path=/work/etc/models/blazeface/blazeface.onnx imgsz=320 ! \
#      textoverlay name=overlay valignment=top halignment=center font-desc="Sans, 14" ! \
#      ampsink name=sink \
#    dec. ! queue ! audio/x-raw ! audioconvert ! audioresample ! \
#      sink.audiopad


gst-launch-1.0 \
  filesrc location=/work/etc/images/katana.jpg ! \
  jpegdec ! \
  imagefreeze ! \
  videoconvert ! video/x-raw,format=BGRA ! \
      ampinfer opchain-path=/work/etc/models/yolo/opchain.json active=true ! \
      ampinfer opchain-path=/work/etc/models/ultraface/opchain.json active=true ! \
      ampinfer opchain-path=/work/etc/models/personclassification/opchain.json active=true ! \
      ampinfer opchain-path=/work/etc/models/gazedetection/opchain.json active=true ! \
      ampperformance show-all-metrics=true x-offset=20 y-offset=20 font-size=18 alpha=0.9 update-interval=1 ! \
      amposd enabled=true ! \
      ampsink name=sink
  
  msg_end "Pipeline finished."
      #textoverlay name=overlay valignment=top halignment=center font-desc="Sans, 14" ! \
}

audio() {

  msg_begin "Executing test with audio..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR/meson-out"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  export GST_DEBUG_DUMP_DOT_DIR=/work/graphs/
  rm -rf /work/graphs/*.dot

  msg "Running test pipeline.."

# external audio
# valgrind --leak-check=full --num-callers=20 --log-file=vgdump.txt \
# gst-launch-1.0 \
#   videotestsrc is-live=true pattern=ball ! \
#     video/x-raw,framerate=30/1 ! \
#     videoconvert ! \
#     ampsink name=sink \
#   audiotestsrc is-live=true wave=square ! \
#     audio/x-raw,rate=48000,channels=2 ! \
#     sink.audiosink

gst-launch-1.0 \
  videotestsrc is-live=true pattern=ball ! \
    video/x-raw,framerate=30/1 ! \
    videoconvert ! \
    ampsink name=sink \
  audiotestsrc is-live=true wave=square ! \
    audio/x-raw,rate=48000,channels=2 ! \
    sink.audiosink

# internal silence generator
# gst-launch-1.0 \
#   videotestsrc is-live=true pattern=ball ! \
#     video/x-raw,framerate=30/1 ! \
#     videoconvert ! \
#     ampsink name=sink

  msg_end "Pipeline finished."
}

# ---- help ----
usage() {
  cat <<EOF

Commands:
  onnx ➡️ Run yolov8n test using onnx framework.
  onnx_rgba ➡️ Run yolov8n int8 test using onnx framework with rgba.
  audio ➡️ Run the audio test 

EOF
}

# ---- entrypoint ----
cmd="${1:-}"
case "$cmd" in
  onnx) onnx ;;
  ocr) ocr ;;
  onnx_rgb) onnx_rgb ;;
  audio) audio ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac
