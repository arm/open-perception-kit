#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

set -euo pipefail

OPK_PIPELINE=${OPK_PIPELINE:-"yolo26n-320"}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../../

exec ./tools/opk-menu "$OPK_PIPELINE"
