#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

REPO_ROOT="$(repo_checks_resolve_repo_root "${SCRIPT_DIR}")"
REPO_CHECKS_HOOK_MARKER="# repo-checks-managed-hook"

usage() {
    cat << EOF
Usage: $0

Initialize the host-side pre-commit flow: validate the local Docker setup,
build the repo-checks image, and install git hooks backed by the dedicated
runtime container.
EOF
}

trap 'repo_checks_on_error "${LINENO}"' ERR

build_repo_checks_image() {
    echo "Building repo-checks image..."
    repo_checks_build_image "${REPO_ROOT}"
}

hook_is_repo_checks_managed() {
    local hook_path="$1"

    [ -f "${hook_path}" ] || return 1
    grep -Fqx "${REPO_CHECKS_HOOK_MARKER}" "${hook_path}" && return 0
    grep -Fq 'exec "${REPO_ROOT}/scripts/pre-commit/run.sh"' "${hook_path}"
}

ensure_hook_is_safe_to_replace() {
    local hook_path="$1"

    if [ ! -e "${hook_path}" ] || [ ! -s "${hook_path}" ]; then
        return
    fi

    hook_is_repo_checks_managed "${hook_path}" && return

    repo_checks_die \
        "Refusing to overwrite existing hook at ${hook_path}. Move it aside or merge it manually."
}

write_hook() {
    local hook_path="$1"
    local hook_mode="${2:-}"

    ensure_hook_is_safe_to_replace "${hook_path}"

    cat > "${hook_path}" << 'EOF'
#!/usr/bin/env bash
# repo-checks-managed-hook
set -euo pipefail

git_without_hook_env() {
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

REPO_ROOT="$(git_without_hook_env rev-parse --show-toplevel)"
EOF

    if [ -n "${hook_mode}" ]; then
        printf 'exec "${REPO_ROOT}/scripts/pre-commit/run.sh" "%s" "$@"\n' "${hook_mode}" >> "${hook_path}"
    else
        printf 'exec "${REPO_ROOT}/scripts/pre-commit/run.sh" "$@"\n' >> "${hook_path}"
    fi

    chmod +x "${hook_path}"
}

install_pre_commit_hook() {
    local hooks_dir=""
    local hook_path=""

    hooks_dir="$(repo_checks_git_hooks_dir "${REPO_ROOT}")"
    hook_path="${hooks_dir}/pre-commit"

    write_hook "${hook_path}"
    echo "Installed pre-commit hook: ${hook_path}"
}

install_commit_msg_hook() {
    local hooks_dir=""
    local hook_path=""

    hooks_dir="$(repo_checks_git_hooks_dir "${REPO_ROOT}")"
    hook_path="${hooks_dir}/commit-msg"

    write_hook "${hook_path}" "commit-msg"
    echo "Installed commit-msg hook: ${hook_path}"
}

install_hooks() {
    install_pre_commit_hook
    install_commit_msg_hook
}

if [ $# -gt 0 ]; then
    case "${1:-}" in
        help | -h | --help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
fi

repo_checks_check_docker_setup
build_repo_checks_image
install_hooks
