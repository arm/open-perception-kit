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

repo_checks_resolve_repo_root() {
    local script_dir="$1"
    repo_checks_git_without_hook_env -C "${script_dir}" rev-parse --show-toplevel
}

repo_checks_git_hooks_dir() {
    local repo_root="$1"
    repo_checks_git_without_hook_env -C "${repo_root}" rev-parse --git-path hooks
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
    local repo_name=""

    repo_name="$(basename "${repo_root}")"
    repo_name="$(
        printf '%s' "${repo_name}" |
            tr '[:upper:]' '[:lower:]' |
            sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//'
    )"
    [ -n "${repo_name}" ] || repo_name="repo"

    printf '%s\n' "${repo_name}-repo-checks"
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

repo_checks_append_mount_if_external() {
    local repo_root="$1"
    local common_dir="$2"
    local -n mount_args_ref="$3"

    case "${common_dir}" in
        "${repo_root}" | "${repo_root}"/*)
            return
            ;;
    esac

    mount_args_ref+=(-v "${common_dir}:${common_dir}")
}

repo_checks_run_image() {
    local repo_root="$1"
    local image_name=""
    local common_dir=""
    local mount_args=()

    shift

    image_name="$(repo_checks_image_name "${repo_root}")"
    common_dir="$(repo_checks_git_common_dir "${repo_root}")"
    mount_args=(-v "${repo_root}:${repo_root}")
    repo_checks_append_mount_if_external "${repo_root}" "${common_dir}" mount_args

    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp/repo-checks-home \
        -w "${repo_root}" \
        "${mount_args[@]}" \
        "${image_name}" \
        "$@"
}
