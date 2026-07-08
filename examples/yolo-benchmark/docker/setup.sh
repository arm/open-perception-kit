#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
COMPOSE_FILE="${SCRIPT_DIR}/compose.yaml"

source "${REPO_ROOT}/scripts/pre-commit/common.sh"

usage() {
    cat << 'EOF'
Usage:
  examples/yolo-benchmark/docker/setup.sh

Builds the Docker Compose runtime image used by the YOLO benchmark wrapper.

Environment:
  YOLO_BENCHMARK_IMAGE_NAME  Runtime image tag override.
  COMPOSE_PROJECT_NAME       Compose project override. Default: amp-dev-forge-yolo-benchmark
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ "${1:-}" != "" ]]; then
    usage >&2
    exit 2
fi

repo_checks_check_docker_setup

cd "${REPO_ROOT}"
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export COMPOSE_PROJECT_NAME="${COMPOSE_PROJECT_NAME:-amp-dev-forge-yolo-benchmark}"

docker compose -f "${COMPOSE_FILE}" build yolo-benchmark
