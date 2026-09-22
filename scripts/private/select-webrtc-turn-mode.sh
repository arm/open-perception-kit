#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLATFORM_ID="${1:-}"

if [[ -z "${PLATFORM_ID}" ]]; then
    detect_output="$("${SCRIPT_DIR}/../quick-start/detect-environment.sh" --shell)"
    eval "${detect_output}"
    PLATFORM_ID="${OPK_PLATFORM_ID}"
fi

case "${PLATFORM_ID}" in
    wsl | macos)
        echo enabled
        ;;
    *)
        echo disabled
        ;;
esac
