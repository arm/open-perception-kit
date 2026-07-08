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
DOCKERFILE="${REPO_ROOT}/scripts/playwright/pages/Dockerfile"

usage() {
    cat << 'EOF'
Usage:
  examples/yolo-benchmark/pages/run.sh publish|cleanup

Environment:
  YOLO_PAGES_IMAGE_NAME           Runtime image tag override.
  YOLO_PAGES_REBUILD=1            Force rebuild of the publisher image.
  YOLO_PAGES_DRY_RUN=1            Generate the site locally without pushing.
  YOLO_PAGES_LOCAL_ARTIFACT_DIR   Local artifacts/yolo-benchmark directory for dry-run publish.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ "${1:-}" != "publish" && "${1:-}" != "cleanup" ]]; then
    usage >&2
    exit 2
fi

if [[ "${YOLO_PAGES_REBUILD:-}" == "1" ]] || ! docker image inspect "${IMAGE_NAME}" > /dev/null 2>&1; then
    docker build -f "${DOCKERFILE}" -t "${IMAGE_NAME}" "$(dirname "${DOCKERFILE}")"
fi

if [[ -n "${YOLO_PAGES_LOCAL_ARTIFACT_DIR:-}" ]]; then
    YOLO_PAGES_LOCAL_ARTIFACT_DIR="$(realpath "${YOLO_PAGES_LOCAL_ARTIFACT_DIR}")"
fi
YOLO_PAGES_SITE_PARENT=""
if [[ -n "${YOLO_PAGES_SITE_DIR:-}" ]]; then
    YOLO_PAGES_SITE_DIR="$(realpath -m "${YOLO_PAGES_SITE_DIR}")"
    YOLO_PAGES_SITE_PARENT="$(dirname "${YOLO_PAGES_SITE_DIR}")"
    mkdir -p "${YOLO_PAGES_SITE_PARENT}"
fi

env_args=(-e HOME=/tmp)
pass_env() {
    local name="$1"
    if [[ -n "${!name:-}" ]]; then
        env_args+=(-e "${name}=${!name}")
    fi
}

for name in \
    GITHUB_REPOSITORY \
    GITHUB_TOKEN \
    GH_TOKEN \
    UPSTREAM_CONCLUSION \
    UPSTREAM_EVENT \
    UPSTREAM_HEAD_BRANCH \
    UPSTREAM_HEAD_REPOSITORY \
    UPSTREAM_HEAD_SHA \
    UPSTREAM_PR_NUMBER \
    UPSTREAM_RUN_ATTEMPT \
    UPSTREAM_RUN_ID \
    YOLO_PAGES_DRY_RUN \
    YOLO_PAGES_LOCAL_ARTIFACT_DIR \
    YOLO_PAGES_RETENTION_DAYS \
    YOLO_PAGES_SITE_DIR \
    YOLO_PAGES_STORAGE_BRANCH; do
    pass_env "${name}"
done

mount_args=(-v "${REPO_ROOT}:${REPO_ROOT}")
add_path_mount() {
    local path="$1"
    local mode="${2:-}"
    case "${path}" in
        "${REPO_ROOT}" | "${REPO_ROOT}"/*) ;;
        *) mount_args+=(-v "${path}:${path}${mode}") ;;
    esac
}
git_dir="$(git -C "${REPO_ROOT}" rev-parse --path-format=absolute --git-dir)"
git_common_dir="$(git -C "${REPO_ROOT}" rev-parse --path-format=absolute --git-common-dir)"
add_path_mount "${git_common_dir}"
case "${git_dir}" in
    "${git_common_dir}" | "${git_common_dir}"/*) ;;
    *) add_path_mount "${git_dir}" ;;
esac
if [[ -n "${YOLO_PAGES_LOCAL_ARTIFACT_DIR:-}" ]]; then
    add_path_mount "${YOLO_PAGES_LOCAL_ARTIFACT_DIR}" ":ro"
fi
if [[ -n "${YOLO_PAGES_SITE_DIR:-}" ]]; then
    add_path_mount "${YOLO_PAGES_SITE_PARENT}"
fi
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    output_dir="$(dirname "${GITHUB_OUTPUT}")"
    mount_args+=(-v "${output_dir}:${output_dir}")
    env_args+=(-e "GITHUB_OUTPUT=${GITHUB_OUTPUT}")
fi

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -w "${REPO_ROOT}" \
    "${mount_args[@]}" \
    "${env_args[@]}" \
    "${IMAGE_NAME}" \
    "${REPO_ROOT}/examples/yolo-benchmark/pages/publish.sh" "$@"
