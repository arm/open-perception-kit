#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./scripts/peksink-web.sh <generate|check|test>

Commands:
  generate  Rebuild the committed browser bundle.
  check     Rebuild temporarily and fail when the bundle has drifted.
  test      Run the WebUI unit tests.
EOF
}

if [[ $# -ne 1 || "$1" == "-h" || "$1" == "--help" ]]; then
    usage
    [[ $# -eq 1 ]] && exit 0
    exit 2
fi

case "$1" in
    generate | check)
        exec npm --prefix development/web run "$1"
        ;;
    test)
        exec npm --prefix development/web test
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
