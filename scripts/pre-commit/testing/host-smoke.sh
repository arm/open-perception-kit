#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

REPO_ROOT="$(repo_checks_resolve_repo_root "${SCRIPT_DIR}")"
WORK_DIR=""
SMOKE_TMP_PARENT="${REPO_CHECKS_SMOKE_TMP_PARENT:-}"
if [ -n "${SMOKE_TMP_PARENT}" ]; then
    mkdir -p "${SMOKE_TMP_PARENT}"
    WORK_DIR="$(mktemp -d "${SMOKE_TMP_PARENT%/}/repo-checks-smoke.XXXXXX")"
else
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/repo-checks-smoke.XXXXXX")"
fi
SMOKE_REPO="${WORK_DIR}/repo"
FIXTURE_ROOT="${REPO_ROOT}/tools/expkits-ci/tests/fixtures"
PRECOMMIT_CASES=()

cleanup() {
    [ -n "${WORK_DIR:-}" ] && rm -rf "${WORK_DIR}"
}

trap cleanup EXIT

expkits_ci_e2e_module_path() {
    printf '%s\n' "${REPO_ROOT}/tools/expkits-ci/tests/test_expkits_ci_e2e.py"
}

load_formatter_cases() {
    local module_path=""

    module_path="$(expkits_ci_e2e_module_path)"
    MODULE_PATH="${module_path}" python3 - << 'PY'
import importlib.util
import os
import sys

module_path = os.environ["MODULE_PATH"]
spec = importlib.util.spec_from_file_location("expkits_ci_e2e", module_path)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

for case in module.FORMATTER_CASES:
    print(f"{case.target_path}|{case.input_fixture}|{case.expected_fixture}")
PY
}

copy_runtime_files() {
    mkdir -p "${SMOKE_REPO}/scripts/pre-commit" "${SMOKE_REPO}/tools"

    [ -f "${REPO_ROOT}/.dockerignore" ] && cp "${REPO_ROOT}/.dockerignore" "${SMOKE_REPO}/"
    cp "${REPO_ROOT}/.clang-format" "${SMOKE_REPO}/"
    cp "${REPO_ROOT}/.cmake-format.yaml" "${SMOKE_REPO}/"
    cp "${REPO_ROOT}/.secrets.baseline" "${SMOKE_REPO}/"
    cp "${REPO_ROOT}/scripts/pre-commit/common.sh" "${SMOKE_REPO}/scripts/pre-commit/"
    cp "${REPO_ROOT}/scripts/pre-commit/run.sh" "${SMOKE_REPO}/scripts/pre-commit/"
    cp "${REPO_ROOT}/scripts/pre-commit/setup.sh" "${SMOKE_REPO}/scripts/pre-commit/"
    cp -R "${REPO_ROOT}/scripts/pre-commit/runtime" "${SMOKE_REPO}/scripts/pre-commit/"
    cp -R "${REPO_ROOT}/tools/expkits-ci" "${SMOKE_REPO}/tools/"
    cp -R "${REPO_ROOT}/tools/templates" "${SMOKE_REPO}/tools/"
}

copy_case_input() {
    local case_entry="$1"
    local target_path=""
    local input_fixture=""
    local _expected_fixture=""

    IFS='|' read -r target_path input_fixture _expected_fixture <<< "${case_entry}"
    mkdir -p "${SMOKE_REPO}/$(dirname "${target_path}")"
    cp "${FIXTURE_ROOT}/${input_fixture}" "${SMOKE_REPO}/${target_path}"
}

assert_case_matches_expected() {
    local case_entry="$1"
    local target_path=""
    local _input_fixture=""
    local expected_fixture=""

    IFS='|' read -r target_path _input_fixture expected_fixture <<< "${case_entry}"
    cmp -s "${FIXTURE_ROOT}/${expected_fixture}" "${SMOKE_REPO}/${target_path}" || {
        echo "Expected ${target_path} to match ${expected_fixture}." >&2
        exit 1
    }
}

write_secret_file() {
    mkdir -p "${SMOKE_REPO}/secrets"
    MODULE_PATH="$(expkits_ci_e2e_module_path)" python3 - << 'PY' > "${SMOKE_REPO}/secrets/bad.pem"
import importlib.util
import os
import sys

module_path = os.environ["MODULE_PATH"]
spec = importlib.util.spec_from_file_location("expkits_ci_e2e", module_path)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
print(module.make_private_key_fixture(), end="")
PY
}

assert_hook_is_portable() {
    local hook_name="$1"
    local hook_path="${SMOKE_REPO}/.git/hooks/${hook_name}"

    grep -Fq 'git_without_hook_env rev-parse --show-toplevel' "${hook_path}" || {
        echo "Expected ${hook_name} to resolve the repo root at runtime." >&2
        exit 1
    }

    if grep -Fq "${SMOKE_REPO}" "${hook_path}"; then
        echo "Expected ${hook_name} not to bake the repo path into the hook." >&2
        exit 1
    fi
}

assert_hook_exists() {
    local hook_path="$1"

    [ -x "${hook_path}" ] || {
        echo "Expected hook to exist at ${hook_path}." >&2
        exit 1
    }
}

stage_cases() {
    local targets=()
    local case_entry=""
    local target_path=""
    local _input_fixture=""
    local _expected_fixture=""

    for case_entry in "$@"; do
        IFS='|' read -r target_path _input_fixture _expected_fixture <<< "${case_entry}"
        targets+=("${target_path}")
    done

    git add -- "${targets[@]}"
}

init_smoke_repo() {
    local case_entry=""

    mkdir -p "${SMOKE_REPO}"
    copy_runtime_files
    pushd "${SMOKE_REPO}" > /dev/null
    git init -b main > /dev/null
    git config user.name "Repo Checks Smoke"
    git config user.email "repo-checks-smoke@example.com"
    git commit --allow-empty -m "Bootstrap repo-checks smoke base" -m "Task: EXPKITS-941" > /dev/null
    git update-ref refs/remotes/origin/main HEAD
    git checkout -b feature/EXPKITS-941/repo-checks-smoke > /dev/null
    repo_checks_load_lines load_formatter_cases
    PRECOMMIT_CASES=("${REPO_CHECKS_LOADED_LINES[@]}")
    for case_entry in "${PRECOMMIT_CASES[@]}"; do
        copy_case_input "${case_entry}"
    done
    stage_cases "${PRECOMMIT_CASES[@]}"
    popd > /dev/null
}

run_smoke() {
    local case_entry=""
    local custom_hooks_dir=""
    local launcher_dir=""
    local setup_output=""

    pushd "${SMOKE_REPO}" > /dev/null

    ./scripts/pre-commit/setup.sh
    ./scripts/pre-commit/setup.sh
    assert_hook_is_portable "pre-commit"
    assert_hook_is_portable "commit-msg"

    if .git/hooks/pre-commit; then
        echo "Expected the first pre-commit run to fail after rewriting files." >&2
        exit 1
    fi

    for case_entry in "${PRECOMMIT_CASES[@]}"; do
        assert_case_matches_expected "${case_entry}"
    done

    stage_cases "${PRECOMMIT_CASES[@]}"
    .git/hooks/pre-commit

    git commit --no-verify -m "Record clean fixture snapshot" -m "Task: EXPKITS-941" > /dev/null
    ./scripts/pre-commit/run.sh
    ./scripts/pre-commit/run.sh full

    write_secret_file
    git add secrets/bad.pem
    if ./scripts/pre-commit/run.sh; then
        echo "Expected the default delta path to fail on a detected secret." >&2
        exit 1
    fi
    git rm -f secrets/bad.pem > /dev/null
    ./scripts/pre-commit/run.sh

    cat > .git/COMMIT_EDITMSG << 'EOF'
Smoke test commit
Task: EXPKITS-941
EOF
    .git/hooks/commit-msg .git/COMMIT_EDITMSG

    cat > .git/COMMIT_EDITMSG << 'EOF'
Smoke test commit only
EOF
    if .git/hooks/commit-msg .git/COMMIT_EDITMSG; then
        echo "Expected invalid commit message validation to fail." >&2
        exit 1
    fi

    git config core.hooksPath .githooks
    custom_hooks_dir="${SMOKE_REPO}/.githooks"
    launcher_dir="${WORK_DIR}/launcher"
    rm -rf "${custom_hooks_dir}"
    mkdir -p "${launcher_dir}"
    pushd "${launcher_dir}" > /dev/null
    "${SMOKE_REPO}/scripts/pre-commit/setup.sh"
    popd > /dev/null
    assert_hook_exists "${custom_hooks_dir}/pre-commit"
    assert_hook_exists "${custom_hooks_dir}/commit-msg"
    [ ! -e "${launcher_dir}/.githooks" ] || {
        echo "Expected setup.sh not to install hooks relative to the caller working directory." >&2
        exit 1
    }

    cat > "${custom_hooks_dir}/pre-commit" << 'EOF'
#!/usr/bin/env bash
echo "custom hook"
EOF
    chmod +x "${custom_hooks_dir}/pre-commit"

    pushd "${launcher_dir}" > /dev/null
    if setup_output="$("${SMOKE_REPO}/scripts/pre-commit/setup.sh" 2>&1)"; then
        popd > /dev/null
        echo "Expected setup.sh to refuse overwriting an unknown pre-commit hook." >&2
        exit 1
    fi
    popd > /dev/null

    printf '%s\n' "${setup_output}" | grep -Fq "Refusing to overwrite existing hook" || {
        echo "Expected setup.sh to explain why it refused the unknown hook." >&2
        exit 1
    }

    popd > /dev/null
}

init_smoke_repo
run_smoke
