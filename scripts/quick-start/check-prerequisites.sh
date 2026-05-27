#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Placeholder for quick-start host prerequisite checks.
#
# Usage:
#   scripts/quick-start/check-prerequisites.sh
#
# This script intentionally performs no blocking checks yet. It is the extension
# point for the minimal prerequisite set once it has been measured on clean
# virtual machines and target devices.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  check-prerequisites.sh [-h|--help]

Placeholder for PEK quick-start prerequisite checks.

This command currently reports the detected quick-start platform and exits
successfully. Docker, package, permission, and host-tool checks will be added
after the minimal prerequisite set is confirmed.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ "${1:-}" != "" ]]; then
    echo "Error: unknown argument '${1}'" >&2
    echo >&2
    usage >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

eval "$("${SCRIPT_DIR}/detect-environment.sh" --shell)"

echo "Prerequisite checks:"
echo "  Status: placeholder"
echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
echo
echo "No prerequisite checks are enforced yet."
echo "Future checks should validate Docker, Docker Compose, host packages, and user permissions."
