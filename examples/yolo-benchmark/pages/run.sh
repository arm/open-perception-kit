#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Runs the YOLO benchmark Pages publisher in its Docker runtime image.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
IMAGE_NAME="${YOLO_PAGES_IMAGE_NAME:-amp-dev-forge-yolo-benchmark-pages:local}"

if [[ "${1:-}" != "publish" && "${1:-}" != "cleanup" ]]; then
    echo "Usage: examples/yolo-benchmark/pages/run.sh publish|cleanup" >&2
    exit 2
fi

export REPORT_PAGES_REPO_ROOT="${REPO_ROOT}"
export REPORT_PAGES_IMAGE_NAME="${IMAGE_NAME}"
export REPORT_PAGES_REBUILD="${YOLO_PAGES_REBUILD:-}"
export REPORT_PAGES_DOCKERFILE="${REPO_ROOT}/scripts/report_pages/Dockerfile"
export REPORT_PAGES_LOCAL_DIR_ENV="YOLO_PAGES_LOCAL_ARTIFACT_DIR"
export REPORT_PAGES_SITE_DIR_ENV="YOLO_PAGES_SITE_DIR"
export REPORT_PAGES_EXTRA_ENV_NAMES="YOLO_PAGES_DRY_RUN YOLO_PAGES_LOCAL_ARTIFACT_DIR"
REPORT_PAGES_EXTRA_ENV_NAMES+=" YOLO_PAGES_RETENTION_DAYS YOLO_PAGES_SITE_DIR YOLO_PAGES_STORAGE_BRANCH"

exec "${REPO_ROOT}/scripts/report_pages/run_in_docker.sh" \
    python3 "${REPO_ROOT}/examples/yolo-benchmark/pages/publish_yolo_benchmark_pages.py" "$@"
