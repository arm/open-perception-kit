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
setup_env() {
    if [ ! -d "$BUILD_DIR" ]; then
        msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
        exit 1
    fi

    export GST_PLUGIN_PATH="$BUILD_DIR/meson-out"
    msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

    export GST_DEBUG_DUMP_DOT_DIR=/work/graphs/
    mkdir -p "$GST_DEBUG_DUMP_DOT_DIR"
    rm -rf /work/graphs/*.dot
}

# ---- test ----

onnx_rgb() {

  msg_begin "Executing test with ONNX element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  setup_env

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

  setup_env

  msg "Running test pipeline.."

  # IP=$(getent ahostsv4 host.docker.internal | awk 'NR==1{print $1}')
  
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

  msg_begin "Executing test with audio...\n"

  need gst-launch-1.0

  setup_env

  msg "Running test pipeline.."

# external audio
# valgrind --leak-check=full --num-callers=20 --log-file=vgdump.txt \
gst-launch-1.0 \
 videotestsrc is-live=true pattern=ball ! \
   video/x-raw,framerate=30/1 ! \
   videoconvert ! \
   ampsink name=sink \
 audiotestsrc is-live=true wave=square ! \
   audio/x-raw,rate=48000,channels=2 ! \
   sink.audiosink

  msg_end "Pipeline finished."
}

audio_internal_silence() {

    msg_begin "Executing test with audio...\n"

    need gst-launch-1.0

    setup_env

    msg "Running test pipeline.."

    # no audio connected to ampsink -> use the internal silence generator
    gst-launch-1.0 \
        videotestsrc is-live=true pattern=ball ! \
        video/x-raw,framerate=30/1 ! \
        videoconvert ! \
        ampsink name=sink

    msg_end "Pipeline finished."
}

video() {

  msg_begin "Executing test with video...\n"

  need gst-launch-1.0

  setup_env

  msg "Running test pipeline.."

  gst-launch-1.0 \
      filesrc location=/work/etc/videos/00.mp4 ! \
      decodebin name=dec \
          dec. ! queue ! videoconvert ! videoscale ! video/x-raw,framerate=24/1,format=BGRA ! \
              ampinfer opchain-path=/work/etc/models/ultraface/opchain.json active=true ! \
              amposd enabled=true ! \
              ampsink name=sink \
          dec. ! queue ! audioconvert ! audioresample ! audio/x-raw,channels=2 ! sink.audiosink


  msg_end "Pipeline finished."
}

single_cam() {
  msg_begin "Executing test with camera...\n"

  need gst-launch-1.0

  setup_env

  gst-launch-1.0 v4l2src device="$CAM0" do-timestamp=true \
      ! videoconvert ! videoscale \
      ! video/x-raw,width=640,height=480,framerate=30/1 \
      ! ampsink name=sink

  msg_end "Pipeline finished."
}

multi_cam() {
  msg_begin "Executing test with multiple camera...\n"

  need gst-launch-1.0

  setup_env

  gst-launch-1.0 -e \
      compositor name=comp background=black \
          sink_0::xpos=0   sink_0::ypos=0 \
          sink_1::xpos=640 sink_1::ypos=0 \
          ! videoconvert ! ampsink name=sink \
      v4l2src device="$CAM0" do-timestamp=true \
          ! videoconvert ! videoscale \
          ! video/x-raw,width=640,height=480,framerate=30/1 \
          ! queue max-size-time=200000000 \
          ! comp.sink_0 \
      v4l2src device="$CAM1" do-timestamp=true \
          ! videoconvert ! videoscale \
          ! video/x-raw,width=640,height=480,framerate=30/1 \
          ! queue max-size-time=200000000 \
          ! comp.sink_1
  msg_end "Pipeline finished."
}

# ---- help ----
usage() {
  cat <<EOF

Commands:
  onnx                      ➡️ Run yolov8n test using onnx framework.
  onnx_rgb                  ➡️ Run yolov8n int8 test using onnx framework with rgb.
  audio                     ➡️ Run the audio test.
  audio_internal_silence    ➡️ Run the audio test with internal silence generator.
  video                     ➡️ Run the video test.
  single_cam                ➡️ Run test using a /dev/video0 cam  
  multi_cam                 ➡️ Run test using a /dev/video0 and /dev/video4 cam  

EOF
}

# ---- entrypoint ----
cmd="${1:-}"
case "$cmd" in
  onnx) onnx ;;
  ocr) ocr ;;
  onnx_rgb) onnx_rgb ;;
  audio) audio ;;
  audio_internal_silence) audio_internal_silence ;;
  video) video ;;
  single_cam) single_cam ;;
  multi_cam) multi_cam ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac
