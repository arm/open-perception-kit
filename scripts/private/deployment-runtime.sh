#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

OPK_PIPELINE=${OPK_PIPELINE:-"yolo26n-320"}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../../

exec ./tools/opk-menu "$OPK_PIPELINE"
