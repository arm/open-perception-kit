#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
set -Eeuo pipefail

REPO_ROOT="${REPORT_PAGES_REPO_ROOT:-$(git rev-parse --show-toplevel)}"
IMAGE_NAME="${REPORT_PAGES_IMAGE_NAME:?REPORT_PAGES_IMAGE_NAME is required}"
DOCKERFILE="${REPORT_PAGES_DOCKERFILE:-${REPO_ROOT}/Dockerfile.pages}"
COMMON_ENV_NAMES="GITHUB_REPOSITORY GITHUB_TOKEN GH_TOKEN UPSTREAM_CONCLUSION UPSTREAM_EVENT UPSTREAM_HEAD_BRANCH"
COMMON_ENV_NAMES+=" UPSTREAM_HEAD_REPOSITORY UPSTREAM_HEAD_SHA UPSTREAM_PR_NUMBER UPSTREAM_RUN_ATTEMPT UPSTREAM_RUN_ID"

if [[ "${REPORT_PAGES_REBUILD:-}" == "1" ]] || ! docker image inspect "${IMAGE_NAME}" > /dev/null 2>&1; then
    docker build -f "${DOCKERFILE}" -t "${IMAGE_NAME}" "${REPO_ROOT}"
fi

resolve_env_path() {
    local name="$1"
    local value="${!name:-}"
    if [[ -n "${value}" ]]; then
        printf -v "${name}" '%s' "$(realpath "${value}")"
    fi
}

resolve_env_path "${REPORT_PAGES_LOCAL_DIR_ENV:-REPORT_PAGES_UNUSED_LOCAL_DIR}"
if [[ -n "${REPORT_PAGES_SITE_DIR_ENV:-}" && -n "${!REPORT_PAGES_SITE_DIR_ENV:-}" ]]; then
    printf -v "${REPORT_PAGES_SITE_DIR_ENV}" '%s' "$(realpath -m "${!REPORT_PAGES_SITE_DIR_ENV}")"
    REPORT_PAGES_SITE_PARENT="$(dirname "${!REPORT_PAGES_SITE_DIR_ENV}")"
    mkdir -p "${REPORT_PAGES_SITE_PARENT}"
else
    REPORT_PAGES_SITE_PARENT=""
fi

env_args=(-e HOME=/tmp)
pass_env() {
    local name="$1"
    if [[ -n "${!name:-}" ]]; then
        env_args+=(-e "${name}=${!name}")
    fi
}

for name in ${COMMON_ENV_NAMES} ${REPORT_PAGES_EXTRA_ENV_NAMES:-} ${REPORT_PAGES_ENV_NAMES:-}; do
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
if [[ -n "${REPORT_PAGES_LOCAL_DIR_ENV:-}" && -n "${!REPORT_PAGES_LOCAL_DIR_ENV:-}" ]]; then
    add_path_mount "${!REPORT_PAGES_LOCAL_DIR_ENV}" ":ro"
fi
if [[ -n "${REPORT_PAGES_SITE_PARENT}" ]]; then
    add_path_mount "${REPORT_PAGES_SITE_PARENT}"
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
    "$@"
