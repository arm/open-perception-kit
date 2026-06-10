#!/usr/bin/env bash

set -euo pipefail

version_file_path="${1:-./pr-automation-image.env}"
registry_image="${2:-pek-pr-automation}"

if [ ! -f "${version_file_path}" ]; then
    echo "Usage: scripts/build-pr-automation-image.sh [version-file-path] [registry-image]" >&2
    exit 2
fi

set -a
# shellcheck disable=SC1090
. "${version_file_path}"
set +a

if [ -z "${PR_AUTOMATION_IMAGE_VERSION:-}" ]; then
    echo "Missing PR_AUTOMATION_IMAGE_VERSION in ${version_file_path}" >&2
    exit 1
fi

image_ref="${registry_image}:${PR_AUTOMATION_IMAGE_VERSION}"

docker build \
    --target pek-pr-automation \
    -t "${image_ref}" \
    .

if [ -n "${GITHUB_OUTPUT:-}" ]; then
    printf 'image_ref=%s\n' "${image_ref}" >> "${GITHUB_OUTPUT}"
else
    printf '%s\n' "${image_ref}"
fi
