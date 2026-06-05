#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Public quick-start entry point.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/quick_start.sh [-h|--help]

Detects the host environment for the PEK quick-start flow.

This implementation detects the environment, runs the current prerequisite
check, and starts the matching PEK base development container.
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
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DETECT_SCRIPT="${REPO_ROOT}/scripts/quick-start/detect-environment.sh"
PREREQ_SCRIPT="${REPO_ROOT}/scripts/quick-start/check-prerequisites.sh"
START_CONTAINER_SCRIPT="${REPO_ROOT}/scripts/quick-start/start-container.sh"

"${DETECT_SCRIPT}"

echo
"${PREREQ_SCRIPT}"

echo
"${START_CONTAINER_SCRIPT}"

echo
echo "Quick-start container is ready."
