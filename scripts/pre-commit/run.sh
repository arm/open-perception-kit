#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

REPO_ROOT="$(repo_checks_resolve_repo_root "${SCRIPT_DIR}")"
RUNTIME_DOCKERFILE="${REPO_ROOT}/scripts/pre-commit/runtime/Dockerfile"
REPO_CHECKS_COMMAND=()

usage() {
    cat << EOF
Usage: $0 [full]

Host-side wrapper for the dedicated pre-commit runtime.

Modes:
  default              Run the staged-file delta path inside the repo-checks container.
                       If no staged files exist, fall back to the branch delta against
                       PULL_REQUEST_TARGET_BRANCH, origin/HEAD, or main.
  full                 Run the same check bundle against the full tracked worktree.

Internal:
  commit-msg <path>    Run commit message validation for the git commit-msg hook.
EOF
}

trap 'repo_checks_on_error "${LINENO}"' ERR

ensure_runtime_files() {
    [ -f "${RUNTIME_DOCKERFILE}" ] || repo_checks_die "Dockerfile not found: ${RUNTIME_DOCKERFILE}"
}

run_repo_checks_command() {
    repo_checks_run_image "${REPO_ROOT}" "${REPO_CHECKS_COMMAND[@]}"
}

get_staged_files() {
    repo_checks_git_without_hook_env -C "${REPO_ROOT}" diff --cached --name-only --diff-filter=ACMR -z
}

get_branch_delta_files() {
    local target_ref="$1"
    repo_checks_git_without_hook_env -C "${REPO_ROOT}" diff --name-only --diff-filter=ACMR -z "${target_ref}...HEAD"
}

resolve_ref() {
    local ref_name="$1"
    local candidates=(
        "${ref_name}"
        "refs/heads/${ref_name}"
        "refs/remotes/origin/${ref_name}"
    )
    local candidate=""

    for candidate in "${candidates[@]}"; do
        if repo_checks_git_without_hook_env -C "${REPO_ROOT}" rev-parse --verify --quiet "${candidate}" > /dev/null; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

resolve_delta_target_ref() {
    local resolved_ref=""
    local origin_head_ref=""

    if [ -n "${PULL_REQUEST_TARGET_BRANCH:-}" ]; then
        resolved_ref="$(resolve_ref "${PULL_REQUEST_TARGET_BRANCH}" || true)"
        [ -n "${resolved_ref}" ] || repo_checks_die \
            "Could not resolve PULL_REQUEST_TARGET_BRANCH=${PULL_REQUEST_TARGET_BRANCH}"
        printf '%s\n' "${resolved_ref}"
        return
    fi

    origin_head_ref="$(
        repo_checks_git_without_hook_env -C "${REPO_ROOT}" symbolic-ref --quiet refs/remotes/origin/HEAD 2> /dev/null || true
    )"
    if [ -n "${origin_head_ref}" ]; then
        printf '%s\n' "${origin_head_ref}"
        return
    fi

    resolved_ref="$(resolve_ref main || true)"
    if [ -n "${resolved_ref}" ]; then
        printf '%s\n' "${resolved_ref}"
        return
    fi

    repo_checks_die \
        "Could not resolve a delta target branch. Stage files, set PULL_REQUEST_TARGET_BRANCH, or use full."
}

build_commit_msg_command() {
    local files=("$@")

    [ "${#files[@]}" -eq 1 ] || repo_checks_die "commit-msg requires exactly one commit message path."

    REPO_CHECKS_COMMAND=(
        expkits-ci
        --verbose
        --commit-msg
        --list-of-files
        "${files[0]}"
    )
}

build_delta_command() {
    local files=()
    local target_ref=""

    mapfile -d '' -t files < <(get_staged_files)

    if [ "${#files[@]}" -eq 0 ]; then
        target_ref="$(resolve_delta_target_ref)"
        mapfile -d '' -t files < <(get_branch_delta_files "${target_ref}")
    fi

    REPO_CHECKS_COMMAND=(expkits-ci --verbose --branch-naming)

    if [ "${#files[@]}" -eq 0 ]; then
        echo "No staged or branch-delta files found; running branch naming only."
        return
    fi

    REPO_CHECKS_COMMAND+=(
        --clang-format
        --python-format
        --cmake-format
        --license-header
        --check-secrets
        --list-of-files
        "${files[@]}"
    )
}

build_full_command() {
    REPO_CHECKS_COMMAND=(
        expkits-ci
        --verbose
        --branch-naming
        --clang-format
        --python-format
        --cmake-format
        --license-header
        --check-secrets
    )
}

run_host_mode() {
    ensure_runtime_files

    case "${MODE}" in
        delta)
            [ "$#" -eq 0 ] || repo_checks_die "default delta mode does not accept positional arguments."
            build_delta_command
            repo_checks_check_docker_setup
            run_repo_checks_command
            ;;
        commit-msg)
            build_commit_msg_command "$@"
            repo_checks_check_docker_setup
            run_repo_checks_command
            ;;
        full)
            [ "$#" -eq 0 ] || repo_checks_die "full does not accept positional arguments."
            build_full_command
            repo_checks_check_docker_setup
            run_repo_checks_command
            ;;
        *)
            usage
            exit 1
            ;;
    esac
}

MODE="delta"

if [ $# -gt 0 ]; then
    case "${1:-}" in
        help | -h | --help)
            usage
            exit 0
            ;;
        full | commit-msg)
            MODE="$1"
            shift
            ;;
        *)
            usage
            exit 1
            ;;
    esac
fi

run_host_mode "$@"
