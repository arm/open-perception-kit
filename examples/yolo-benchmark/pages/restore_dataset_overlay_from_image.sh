#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Restores the Pages-only benchmark inputs from their OCI data image.
################################################################

set -Eeuo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: restore_dataset_overlay_from_image.sh <site-dir>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
DATASET_IMAGE="${YOLO_PAGES_DATASET_IMAGE:?YOLO_PAGES_DATASET_IMAGE is required}"
CACHE_DIR="$(mktemp -d)"
CONTAINER_ID=""

cleanup() {
    if [[ -n "${CONTAINER_ID}" ]]; then
        docker rm -f "${CONTAINER_ID}" > /dev/null 2>&1 || true
    fi
    docker image rm "${DATASET_IMAGE}" > /dev/null 2>&1 || true
    rm -rf -- "${CACHE_DIR}"
}
trap cleanup EXIT

docker pull "${DATASET_IMAGE}"
CONTAINER_ID="$(docker create "${DATASET_IMAGE}" true)"
docker cp "${CONTAINER_ID}:/opt/yolo-performance-dataset/." "${CACHE_DIR}"
python3 "${REPO_ROOT}/examples/yolo-benchmark/pages/restore_dataset_overlay.py" \
    --site-dir "$1" \
    --cache-dir "${CACHE_DIR}"
