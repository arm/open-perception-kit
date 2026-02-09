#!/usr/bin/env bash

set -euo pipefail

# Test pipeline for MobileNetV2 classification model

export GST_PLUGIN_PATH=/work/development/build

echo "Running MobileNetV2 classification test pipeline..."
echo "Open http://localhost:9999 in your browser to see results"
echo ""

gst-launch-1.0 \
  filesrc location=/work/etc/images/golden_retriever.jpg ! \
  jpegdec ! \
  imagefreeze ! \
  videoconvert ! video/x-raw,format=BGRA ! \
    ampinfer opchain-path=/work/etc/models/mobilenetv2/opchain.json active=true ! \
    ampperformance show-all-metrics=true x-offset=20 y-offset=20 font-size=18 alpha=0.9 update-interval=1 ! \
    amposd enabled=true ! \
    ampsink name=sink

echo "Pipeline finished."
