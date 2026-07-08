#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Runs the Playwright Pages publisher in its Docker runtime image.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
IMAGE_NAME="${PLAYWRIGHT_PAGES_IMAGE_NAME:-amp-dev-forge-playwright-pages:local}"

usage() {
    cat << 'EOF'
Usage:
  scripts/playwright/pages/run.sh publish|cleanup

Environment:
  PLAYWRIGHT_PAGES_IMAGE_NAME  Runtime image tag override.
  PLAYWRIGHT_PAGES_REBUILD=1   Force rebuild of the publisher image.
  PLAYWRIGHT_PAGES_DRY_RUN=1   Generate the site locally without pushing.
  PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR  Local playwright-report directory for dry-run publish.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ "${1:-}" != "publish" && "${1:-}" != "cleanup" ]]; then
    usage >&2
    exit 2
fi

if [[ "${PLAYWRIGHT_PAGES_REBUILD:-}" == "1" ]] || ! docker image inspect "${IMAGE_NAME}" > /dev/null 2>&1; then
    docker build -f "${SCRIPT_DIR}/Dockerfile" -t "${IMAGE_NAME}" "${SCRIPT_DIR}"
fi

if [[ -n "${PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR:-}" ]]; then
    PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR="$(realpath "${PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR}")"
fi
PLAYWRIGHT_PAGES_SITE_PARENT=""
if [[ -n "${PLAYWRIGHT_PAGES_SITE_DIR:-}" ]]; then
    PLAYWRIGHT_PAGES_SITE_DIR="$(realpath -m "${PLAYWRIGHT_PAGES_SITE_DIR}")"
    PLAYWRIGHT_PAGES_SITE_PARENT="$(dirname "${PLAYWRIGHT_PAGES_SITE_DIR}")"
    mkdir -p "${PLAYWRIGHT_PAGES_SITE_PARENT}"
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
    PLAYWRIGHT_PAGES_DRY_RUN \
    PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR \
    PLAYWRIGHT_PAGES_RETENTION_DAYS \
    PLAYWRIGHT_PAGES_SITE_DIR \
    PLAYWRIGHT_PAGES_STORAGE_BRANCH; do
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
if [[ -n "${PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR:-}" ]]; then
    add_path_mount "${PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR}" ":ro"
fi
if [[ -n "${PLAYWRIGHT_PAGES_SITE_DIR:-}" ]]; then
    add_path_mount "${PLAYWRIGHT_PAGES_SITE_PARENT}"
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
    "${REPO_ROOT}/scripts/playwright/pages/publish.sh" "$@"
