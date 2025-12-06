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
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR/meson-out"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  msg "Running test pipeline.."

  IP=$(getent ahostsv4 host.docker.internal | awk 'NR==1{print $1}')
  
  # ampinfer model-path=/work/etc/models/yolov11n/yolo11n-fp32-320.onnx imgsz=320 ! \
  # ampinfer model-path=/work/etc/models/blazeface/blaze-fp32-128.onnx imgsz=320 ! \

  gst-launch-1.0 \
    filesrc location=/work/etc/videos/00.mp4 ! decodebin name=dec \
    dec. ! queue ! video/x-raw ! videoconvert ! \
      ampinfer model-path=/work/etc/models/blazeface/blazeface.onnx imgsz=320 ! \
      textoverlay name=overlay valignment=top halignment=center font-desc="Sans, 14" ! \
      ampsink name=sink \
    dec. ! queue ! audio/x-raw ! audioconvert ! audioresample ! \
      sink.audiopad

  msg_end "Pipeline finished."
}

#    videoscale ! video/x-raw,format=RGB,width=160,height=160 ! \


#  gst-launch-1.0 \
#    filesrc location=/work/etc/videos/00.mp4 ! decodebin ! \
#    videoconvert ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! \
#    ampinfer model-path=/work/etc/models/yolov8n/yolov8n-fp32.onnx imgsz=640 ! \
#    videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! \
#    mpegtsmux ! \
#    udpsink host="$IP" port=5000 sync=false async=false

onnx2() {

  msg_begin "Executing test with ONNX2 element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  msg "Running test pipeline.."

  IP=$(getent ahostsv4 host.docker.internal | awk 'NR==1{print $1}')
  
  gst-launch-1.0 \
    filesrc location=/work/etc/videos/00.mp4 ! decodebin ! \
    videoconvert ! videoscale ! video/x-raw,format=RGB,width=160,height=160 ! \
    ampinfer model-path=/work/etc/models/yolov8n/yolov8n-160-qdq.onnx imgsz=160 ! \
    videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! \
    mpegtsmux ! \
    udpsink host="$IP" port=5000 sync=false async=false

  msg_end "Pipeline finished."
}

ampinfer() {

  msg_begin "Executing test with AMPINFER element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
    exit 1
  fi

  export GST_PLUGIN_PATH="$BUILD_DIR"
  msg "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

  msg "Running test pipeline.."

  IP=$(getent ahostsv4 host.docker.internal | awk 'NR==1{print $1}')
  
  gst-launch-1.0 \
    filesrc location=/work/etc/videos/00.mp4 ! decodebin ! \
    ampinferonnx model-path=/work/etc/models/yolov8n/yolov8n-fp32.onnx imgsz=640 ! \
    videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! \
    mpegtsmux ! \
    udpsink host="$IP" port=5000 sync=false async=false

  msg_end "Pipeline finished."
}

#    ampinfer model-path=/work/etc/models/yolov8n/yolov8n-fp32.onnx imgsz=640 ! \
#    videoconvert ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! \

onnxweb() {

  msg_begin "Executing test over web with ONNX element..\n"

  need gst-launch-1.0

  if [ ! -d "$BUILD_DIR" ]; then
    msg_end_err "Error: directory $BUILD_DIR does not exist" >&2
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
    videoconvert ! \
    jpegenc quality=75 ! \
    multipartmux boundary=spion ! tcpserversink host=127.0.0.1 port=5001
      #x264enc tune=zerolatency speed-preset=ultrafast ! \
    #mpegtsmux ! \
    #videotestsrc is-live=true ! videoconvert ! vp8enc deadline=1 ! rtpvp8pay ! queue ! send.
 #    hlssink2 target-duration=2 max-files=5 \
#    playlist-location=/var/www/html/stream.m3u8 \
#    location=/var/www/html/segment_%05d.ts \
#    playlist-root=http://127.0.0.1:8080/


    msg_end "Pipeline finished."
}

# ---- help ----
usage() {
  cat <<EOF

Commands:
  onnx ➡️ Run yolov8n test using onnx framework.
  onnx2 ➡️ Run yolov8n int8 test using onnx framework.
  ampinfer ➡️ Run yolov8n test using onnx via ampinfer.
  onnxweb ➡️ Run yolov8n test over web using onnx framework.

EOF
}

# ---- entrypoint ----
cmd="${1:-}"
case "$cmd" in
  onnx) onnx ;;
  onnx2) onnx2 ;;
  ampinfer) ampinfer ;;
  onnxweb) onnxweb ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac