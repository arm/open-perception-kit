#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

REPO_ROOT="$(repo_checks_resolve_repo_root "${SCRIPT_DIR}")"
RUNTIME_DOCKERFILE="${REPO_ROOT}/Dockerfile.pre-commit"
REPO_CHECKS_COMMAND=()

usage() {
    cat << EOF
Usage: $0 [full]

Host-side wrapper for the dedicated pre-commit runtime.

Modes:
  default              Run the staged-file delta path inside the repo-checks container.
                       If no staged files exist, fall back to the branch delta against
                       PULL_REQUEST_TARGET_BRANCH, branch merge-base config, or remote default.
  full                 Run the same check bundle against the full tracked worktree.

Internal:
  commit-msg <path>    Run commit message validation for the git commit-msg hook.
EOF
}

check_retired_model_artifacts() {
    local artifact=""

    while IFS= read -r -d '' artifact; do
        case "${artifact##*.}" in
            [hH][eE][fF]) repo_checks_die "Retired model artifact found: ${artifact#"${REPO_ROOT}/"}" ;;
        esac
    done < <(find "${REPO_ROOT}/config/models" -type f -print0)
}

trap 'repo_checks_on_error "${LINENO}"' ERR

ensure_runtime_files() {
    [ -f "${RUNTIME_DOCKERFILE}" ] || repo_checks_die "Dockerfile not found: ${RUNTIME_DOCKERFILE}"
}

run_repo_checks_command() {
    repo_checks_build_image "${REPO_ROOT}"
    repo_checks_run_image "${REPO_ROOT}" "${REPO_CHECKS_COMMAND[@]}"
}

resolve_ref() {
    local ref_name="$1"
    local candidates=()
    local candidate=""

    case "${ref_name}" in
        refs/*)
            candidates=("${ref_name}")
            ;;
        origin/*)
            candidates=("refs/remotes/${ref_name}")
            ;;
        *)
            candidates=(
                "refs/heads/${ref_name}"
                "refs/remotes/origin/${ref_name}"
            )
            ;;
    esac

    for candidate in "${candidates[@]}"; do
        if repo_checks_git_without_hook_env -C "${REPO_ROOT}" show-ref --verify --quiet -- "${candidate}"; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

resolve_current_branch_merge_base_ref() {
    local branch_name=""
    local merge_base_ref=""
    local resolved_ref=""

    branch_name="$(
        repo_checks_git_without_hook_env -C "${REPO_ROOT}" symbolic-ref --quiet --short HEAD 2> /dev/null || true
    )"
    [ -n "${branch_name}" ] || return 1

    merge_base_ref="$(
        repo_checks_git_without_hook_env -C "${REPO_ROOT}" config --get "branch.${branch_name}.vscode-merge-base" 2> /dev/null || true
    )"
    [ -n "${merge_base_ref}" ] || return 1

    resolved_ref="$(resolve_ref "${merge_base_ref}" || true)"
    [ -n "${resolved_ref}" ] || return 1

    printf '%s\n' "${resolved_ref}"
}

resolve_remote_default_ref() {
    local remote_ref=""
    local resolved_ref=""
    local origin_head_ref=""

    remote_ref="$(
        repo_checks_git_without_hook_env -C "${REPO_ROOT}" ls-remote --symref origin HEAD 2> /dev/null |
            sed -n 's#^ref: refs/heads/\([^[:space:]]*\)[[:space:]]HEAD$#origin/\1#p' |
            sed -n '1p' || true
    )"
    if [ -n "${remote_ref}" ]; then
        resolved_ref="$(resolve_ref "${remote_ref}" || true)"
        if [ -n "${resolved_ref}" ]; then
            printf '%s\n' "${resolved_ref}"
            return
        fi
    fi

    origin_head_ref="$(
        repo_checks_git_without_hook_env -C "${REPO_ROOT}" symbolic-ref --quiet refs/remotes/origin/HEAD 2> /dev/null || true
    )"
    [ -n "${origin_head_ref}" ] || return 1

    printf '%s\n' "${origin_head_ref}"
}

resolve_delta_target_ref() {
    local resolved_ref=""

    if [ -n "${PULL_REQUEST_TARGET_BRANCH:-}" ]; then
        resolved_ref="$(resolve_ref "${PULL_REQUEST_TARGET_BRANCH}" || true)"
        [ -n "${resolved_ref}" ] || repo_checks_die \
            "Could not resolve PULL_REQUEST_TARGET_BRANCH=${PULL_REQUEST_TARGET_BRANCH}"
        printf '%s\n' "${resolved_ref}"
        return
    fi

    resolved_ref="$(resolve_current_branch_merge_base_ref || true)"
    if [ -n "${resolved_ref}" ]; then
        printf '%s\n' "${resolved_ref}"
        return
    fi

    resolved_ref="$(resolve_remote_default_ref || true)"
    if [ -n "${resolved_ref}" ]; then
        printf '%s\n' "${resolved_ref}"
        return
    fi

    repo_checks_die \
        "Could not resolve a delta target branch. Stage files, set PULL_REQUEST_TARGET_BRANCH, configure branch merge-base, or use full."
}

build_commit_msg_command() {
    local files=("$@")

    [ "${#files[@]}" -eq 1 ] || repo_checks_die "commit-msg requires exactly one commit message path."

    REPO_CHECKS_COMMAND=(
        opk-ci
        --verbose
        --commit-msg
        --list-of-files
        "${files[0]}"
    )
}

build_delta_command() {
    local files=()
    local target_ref=""

    repo_checks_load_null_delimited_paths \
        repo_checks_git_without_hook_env -C "${REPO_ROOT}" diff --cached --name-only --diff-filter=ACMR -z
    files=("${REPO_CHECKS_LOADED_PATHS[@]+"${REPO_CHECKS_LOADED_PATHS[@]}"}")

    if [ "${#files[@]}" -eq 0 ]; then
        target_ref="$(resolve_delta_target_ref)"
        repo_checks_load_null_delimited_paths \
            repo_checks_git_without_hook_env -C "${REPO_ROOT}" diff --name-only --diff-filter=ACMR -z "${target_ref}...HEAD"
        files=("${REPO_CHECKS_LOADED_PATHS[@]+"${REPO_CHECKS_LOADED_PATHS[@]}"}")
    fi

    REPO_CHECKS_COMMAND=(opk-ci --verbose --branch-naming)

    if [ "${#files[@]}" -eq 0 ]; then
        echo "No staged or branch-delta files found; running branch naming only."
        return
    fi

    REPO_CHECKS_COMMAND+=(
        --pre-commit-fix
        --list-of-files
        "${files[@]}"
    )
}

build_full_command() {
    REPO_CHECKS_COMMAND=(
        opk-ci
        --verbose
        --branch-naming
        --pre-commit-fix
    )
}

run_host_mode() {
    ensure_runtime_files

    case "${MODE}" in
        delta)
            [ "$#" -eq 0 ] || repo_checks_die "default delta mode does not accept positional arguments."
            check_retired_model_artifacts
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
            check_retired_model_artifacts
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
