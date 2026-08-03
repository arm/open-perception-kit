#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

PEK_PIPELINE=${PEK_PIPELINE:-"yolov11-onnx"}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../../

exec ./tools/pek-menu "$PEK_PIPELINE"
