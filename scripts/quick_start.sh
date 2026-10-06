#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Public quick-start entry point.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/quick_start.sh [-h|--help]

Detects the host environment for the OPK quick-start flow.

This implementation detects the environment, runs the current prerequisite
check, and starts the matching OPK base development container. When present,
the repository-root .env file is passed to Docker Compose.
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
START_CONTAINER_ARGS=()
if [[ -f "${REPO_ROOT}/.env" ]]; then
    START_CONTAINER_ARGS=(--env-file "${REPO_ROOT}/.env")
fi
"${START_CONTAINER_SCRIPT}" "${START_CONTAINER_ARGS[@]}"

echo
echo "Quick-start container is ready."
