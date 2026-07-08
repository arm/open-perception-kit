#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

repo_checks_git_without_hook_env() {
    env \
        -u GIT_DIR \
        -u GIT_WORK_TREE \
        -u GIT_INDEX_FILE \
        -u GIT_PREFIX \
        -u GIT_COMMON_DIR \
        -u GIT_OBJECT_DIRECTORY \
        -u GIT_ALTERNATE_OBJECT_DIRECTORIES \
        git "$@"
}

repo_checks_on_error() {
    local line_number="$1"
    echo "Error at line ${line_number}: \`${BASH_COMMAND}\`" >&2
}

repo_checks_die() {
    echo "Error: $1" >&2
    exit 1
}

repo_checks_remove_file() {
    local path="${1:-}"
    [ -n "${path}" ] && rm -f -- "${path}"
}

repo_checks_resolve_temp_parent() {
    local tmp_parent="${1:-${TMPDIR:-/tmp}}"

    mkdir -p -- "${tmp_parent}" || return 1
    (   
        cd -- "${tmp_parent}" > /dev/null &&
            pwd
    )
}

repo_checks_create_temp_file() {
    local tmp_parent="${1:-${TMPDIR:-/tmp}}"
    local resolved_tmp_parent=""
    local temp_name=""

    resolved_tmp_parent="$(repo_checks_resolve_temp_parent "${tmp_parent}")" || return 1
    temp_name="$(
        cd -- "${resolved_tmp_parent}" > /dev/null &&
            mktemp "repo-checks.XXXXXX"
    )" || return 1

    printf '%s\n' "${resolved_tmp_parent}/${temp_name}"
}

repo_checks_create_temp_dir() {
    local tmp_parent="${1:-${TMPDIR:-/tmp}}"
    local resolved_tmp_parent=""
    local temp_name=""

    resolved_tmp_parent="$(repo_checks_resolve_temp_parent "${tmp_parent}")" || return 1
    temp_name="$(
        cd -- "${resolved_tmp_parent}" > /dev/null &&
            mktemp -d "repo-checks.XXXXXX"
    )" || return 1

    printf '%s\n' "${resolved_tmp_parent}/${temp_name}"
}

repo_checks_resolve_repo_root() {
    local script_dir="$1"
    repo_checks_git_without_hook_env -C "${script_dir}" rev-parse --show-toplevel
}

repo_checks_git_hooks_dir() {
    local repo_root="$1"
    local hooks_dir=""

    hooks_dir="$(repo_checks_git_without_hook_env -C "${repo_root}" rev-parse --git-path hooks)"
    case "${hooks_dir}" in
        /*)
            printf '%s\n' "${hooks_dir}"
            ;;
        *)
            printf '%s\n' "${repo_root}/${hooks_dir}"
            ;;
    esac
}

repo_checks_git_common_dir() {
    local repo_root="$1"
    local common_dir=""

    common_dir="$(repo_checks_git_without_hook_env -C "${repo_root}" rev-parse --git-common-dir)"
    case "${common_dir}" in
        /*)
            printf '%s\n' "${common_dir}"
            ;;
        *)
            printf '%s\n' "${repo_root}/${common_dir}"
            ;;
    esac
}

repo_checks_check_docker_setup() {
    if ! command -v docker > /dev/null 2>&1; then
        repo_checks_die "Docker is not installed or not on PATH."
    fi

    if ! docker version > /dev/null 2>&1; then
        repo_checks_die "The Docker CLI is available, but Docker is not responding."
    fi

    if ! docker info > /dev/null 2>&1; then
        repo_checks_die "Docker is installed, but the daemon is not reachable for the current user."
    fi
}

repo_checks_image_name() {
    local repo_root="$1"
    local image_name="${REPO_CHECKS_IMAGE_NAME:-}"
    local repo_name=""

    if [ -n "${image_name}" ]; then
        printf '%s\n' "${image_name}"
        return
    fi

    repo_name="$(basename "${repo_root}")"
    repo_name="$(
        printf '%s' "${repo_name}" |
            tr '[:upper:]' '[:lower:]' |
            sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//'
    )"
    [ -n "${repo_name}" ] || repo_name="repo"

    printf '%s\n' "${repo_name}-repo-checks"
}

# Keep the host-side flow compatible with older Bash releases by avoiding
# Bash 4+ helpers such as mapfile and namerefs.
repo_checks_load_lines() {
    local tmp_file=""
    local line=""

    REPO_CHECKS_LOADED_LINES=()
    tmp_file="$(repo_checks_create_temp_file)" || repo_checks_die "Could not create a temporary file."

    if ! "$@" > "${tmp_file}"; then
        repo_checks_remove_file "${tmp_file}"
        return 1
    fi

    while IFS= read -r line || [ -n "${line}" ]; do
        REPO_CHECKS_LOADED_LINES+=("${line}")
    done < "${tmp_file}"

    repo_checks_remove_file "${tmp_file}"
}

repo_checks_load_null_delimited_paths() {
    local tmp_file=""
    local path=""

    REPO_CHECKS_LOADED_PATHS=()
    tmp_file="$(repo_checks_create_temp_file)" || repo_checks_die "Could not create a temporary file."

    if ! "$@" > "${tmp_file}"; then
        repo_checks_remove_file "${tmp_file}"
        return 1
    fi

    while IFS= read -r -d '' path; do
        REPO_CHECKS_LOADED_PATHS+=("${path}")
    done < "${tmp_file}"

    repo_checks_remove_file "${tmp_file}"
}

repo_checks_build_image() {
    local repo_root="$1"
    local image_name=""
    local dockerfile_path=""

    image_name="$(repo_checks_image_name "${repo_root}")"
    dockerfile_path="${repo_root}/scripts/pre-commit/runtime/Dockerfile"

    [ -f "${dockerfile_path}" ] || repo_checks_die "Dockerfile not found: ${dockerfile_path}"

    docker build \
        -f "${dockerfile_path}" \
        -t "${image_name}" \
        "${repo_root}"
}

repo_checks_require_built_image() {
    local image_name="$1"

    if ! docker image inspect "${image_name}" > /dev/null 2>&1; then
        repo_checks_die \
            "Repo-checks image '${image_name}' is not built yet. From the repository root, run ./scripts/pre-commit/setup.sh first."
    fi
}

repo_checks_run_image() {
    local repo_root="$1"
    local image_name=""
    local common_dir=""
    local mount_args=()

    shift

    image_name="$(repo_checks_image_name "${repo_root}")"
    repo_checks_require_built_image "${image_name}"
    common_dir="$(repo_checks_git_common_dir "${repo_root}")"
    mount_args=(-v "${repo_root}:${repo_root}")
    case "${common_dir}" in
        "${repo_root}" | "${repo_root}"/*) ;;
        *)
            mount_args+=(-v "${common_dir}:${common_dir}")
            ;;
    esac

    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0="${repo_root}" \
        -w "${repo_root}" \
        "${mount_args[@]}" \
        "${image_name}" \
        "$@"
}
