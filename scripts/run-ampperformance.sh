#!/bin/bash
# Helper script to run ampperformance with correct library paths

export GST_PLUGIN_PATH=/work/development/build/meson-out
export LD_LIBRARY_PATH=/work/development/build/meson-out:$LD_LIBRARY_PATH

# Parse options
SHOW_ALL_METRICS="false"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --show-all-metrics=*)
      SHOW_ALL_METRICS="${1#*=}"
      shift
      ;;
    --show-all-metrics)
      SHOW_ALL_METRICS="true"
      shift
      ;;
    --predefined-metrics)
      SHOW_ALL_METRICS="false"
      shift
      ;;
    *)
      break
      ;;
  esac
done

echo "=== AMP Performance Overlay Pipeline ==="
echo ""
echo "Plugin path: $GST_PLUGIN_PATH"
echo "Library path: $LD_LIBRARY_PATH"
echo "Show all metrics: $SHOW_ALL_METRICS"
echo ""

if [ $# -eq 0 ]; then
  echo "Usage: $0 [options] <command>"
  echo ""
  echo "Options:"
  echo "  --show-all-metrics[=true|false]  Show all metrics (default: true)"
  echo "  --predefined-metrics             Show only predefined metrics (same as --show-all-metrics=false)"
  echo ""
  echo "Examples:"
  echo "  # Test with videotestsrc"
  echo "  $0 test"
  echo "  $0 --predefined-metrics test"
  echo ""
  echo "  # Process a video file"
  echo "  $0 video <input.mp4> <output.mp4>"
  echo "  $0 --show-all-metrics=false video <input.mp4> <output.mp4>"
  echo ""
  echo "  # Custom pipeline"
  echo "  $0 gst-launch-1.0 videotestsrc ! ..."
  exit 1
fi

case "$1" in
  test)
    echo "Running test pipeline..."
    gst-launch-1.0 \
      videotestsrc num-buffers=150 pattern=smpte ! \
      video/x-raw,format=RGB,width=1280,height=720 ! \
      ampinfer model-path=/work/etc/models/yolo/yolo.json ! \
      videoconvert ! video/x-raw,format=BGRA ! \
      ampperformance show-all-metrics=$SHOW_ALL_METRICS x-offset=20 y-offset=20 font-size=18 alpha=0.9 update-interval=1 ! \
      videoconvert ! x264enc ! mp4mux ! \
      filesink location=/work/test_ampperformance.mp4
    echo ""
    echo "✓ Output: /work/test_ampperformance.mp4"
    ;;
    
  video)
    if [ -z "$2" ] || [ -z "$3" ]; then
      echo "Error: video command requires input and output files"
      echo "Usage: $0 video <input.mp4> <output.mp4>"
      exit 1
    fi
    INPUT="$2"
    OUTPUT="$3"
    
    if [ ! -f "$INPUT" ]; then
      echo "Error: Input file not found: $INPUT"
      exit 1
    fi
    
    echo "Processing video: $INPUT -> $OUTPUT"
    gst-launch-1.0 \
      filesrc location="$INPUT" ! qtdemux ! h264parse ! avdec_h264 ! \
      videoconvert ! video/x-raw,format=RGB ! \
      videoscale ! video/x-raw,width=1280,height=720 ! \
      ampinfer model-path=/work/etc/models/yolov/yolo.json  ! \
      videoconvert ! video/x-raw,format=BGRA ! \
      ampperformance show-all-metrics=$SHOW_ALL_METRICS x-offset=20 y-offset=20 font-size=18 alpha=0.9 ! \
      videoconvert ! x264enc ! mp4mux ! \
      filesink location="$OUTPUT"
    echo ""
    echo "✓ Output: $OUTPUT"
    ;;
    
  gst-launch-1.0)
    # Execute gst-launch-1.0 with all remaining arguments
    "$@"
    ;;
    
  *)
    # Treat first argument as input video, second as output (simplified syntax)
    if [ -f "$1" ]; then
      INPUT="$1"
      OUTPUT="${2:-output.mp4}"
      
      echo "Processing video: $INPUT -> $OUTPUT"
      gst-launch-1.0 \
        filesrc location="$INPUT" ! qtdemux ! h264parse ! avdec_h264 ! \
        videoconvert ! video/x-raw,format=RGB ! \
        videoscale ! video/x-raw,width=1280,height=720 ! \
        ampinfer model-path=/work/etc/models/yolo/yolo.json  ! \
        videoconvert ! video/x-raw,format=BGRA ! \
        ampperformance show-all-metrics=$SHOW_ALL_METRICS x-offset=20 y-offset=20 font-size=18 alpha=0.9 ! \
        videoconvert ! x264enc ! mp4mux ! \
        filesink location="$OUTPUT"
      echo ""
      echo "✓ Output: $OUTPUT"
    else
      echo "Error: Unknown command or file not found: $1"
      echo "Run '$0' without arguments for usage help"
      exit 1
    fi
    ;;
esac
