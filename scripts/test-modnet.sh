#!/usr/bin/env bash

set -euo pipefail

# Test script for MODNet portrait matting model

export GST_PLUGIN_PATH=/work/development/build/meson-out
export LD_LIBRARY_PATH=/work/development/build/meson-out:${LD_LIBRARY_PATH:-}

echo "Running MODNet portrait matting test..."
echo "Open http://localhost:9999 in your browser to see video"
echo ""

gst-launch-1.0 \
  filesrc location=/work/etc/images/woman.jpg ! \
  jpegdec ! \
  imagefreeze ! \
  videoconvert ! video/x-raw,format=BGRA ! \
    ampinfer opchain-path=/work/etc/models/modnet/opchain.json active=true ! \
    ampperformance show-all-metrics=true x-offset=20 y-offset=20 font-size=18 alpha=0.9 update-interval=1 ! \
    amposd enabled=true ! \
    ampsink name=sink

echo "Pipeline finished."
