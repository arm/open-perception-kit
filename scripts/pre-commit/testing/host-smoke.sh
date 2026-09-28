#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

if ! command -v python3 > /dev/null 2>&1; then
    repo_checks_die "host-smoke.sh requires python3 on the host to load opk-ci fixtures."
fi

REPO_ROOT="$(repo_checks_resolve_repo_root "${SCRIPT_DIR}")"
WORK_DIR=""
SMOKE_TMP_PARENT="${REPO_CHECKS_SMOKE_TMP_PARENT:-}"
WORK_DIR="$(repo_checks_create_temp_dir "${SMOKE_TMP_PARENT:-${TMPDIR:-/tmp}}")"
export REPO_CHECKS_IMAGE_NAME="repo-checks-smoke:$(basename -- "${WORK_DIR}")"
SMOKE_REPO="${WORK_DIR}/repo"
FIXTURE_ROOT="${REPO_ROOT}/tools/opk-ci/tests/fixtures"
PRECOMMIT_CASES=()

cleanup() {
    local work_dir="${WORK_DIR:-}"

    [ -n "${work_dir}" ] || return
    [ ! -L "${work_dir}" ] || repo_checks_die "Refusing to clean up symlinked work directory: ${work_dir}"

    case "$(basename -- "${work_dir}")" in
        repo-checks.*) ;;
        *)
            repo_checks_die "Refusing to clean up unexpected work directory: ${work_dir}"
            ;;
    esac

    [ -d "${work_dir}" ] || return
    find "${work_dir}" -mindepth 1 -delete
    rmdir -- "${work_dir}"
}

trap cleanup EXIT

opk_ci_e2e_module_path() {
    printf '%s\n' "${REPO_ROOT}/tools/opk-ci/tests/test_opk_ci_e2e.py"
}

load_formatter_cases() {
    local module_path=""

    module_path="$(opk_ci_e2e_module_path)"
    MODULE_PATH="${module_path}" python3 - << 'PY'
import importlib.util
import os
import sys

module_path = os.environ["MODULE_PATH"]
spec = importlib.util.spec_from_file_location("opk_ci_e2e", module_path)
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
    cp -R "${REPO_ROOT}/tools/opk-ci" "${SMOKE_REPO}/tools/"
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
    cmp -s <(sed "s/\[year\]/$(date +%Y)/g" "${FIXTURE_ROOT}/${expected_fixture}") "${SMOKE_REPO}/${target_path}" || {
        echo "Expected ${target_path} to match ${expected_fixture}." >&2
        exit 1
    }
}

write_secret_file() {
    mkdir -p "${SMOKE_REPO}/secrets"
    MODULE_PATH="$(opk_ci_e2e_module_path)" python3 - << 'PY' > "${SMOKE_REPO}/secrets/bad.pem"
import importlib.util
import os
import sys

module_path = os.environ["MODULE_PATH"]
spec = importlib.util.spec_from_file_location("opk_ci_e2e", module_path)
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
    git init -b develop > /dev/null
    git config user.name "Repo Checks Smoke"
    git config user.email "repo-checks-smoke@example.com"
    git commit --allow-empty -m "Bootstrap repo-checks smoke base" -m "Task: EXPKITS-941" > /dev/null
    git update-ref refs/remotes/origin/develop HEAD
    git checkout -b feature/EXPKITS-941/repo-checks-smoke > /dev/null
    git config branch.feature/EXPKITS-941/repo-checks-smoke.vscode-merge-base origin/develop
    git symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main
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
    local default_user=""
    local launcher_dir=""
    local setup_output=""
    local temp_parent=""
    local temp_file=""
    local temp_dir=""
    local symlink_target=""

    pushd "${SMOKE_REPO}" > /dev/null

    ./scripts/pre-commit/setup.sh
    ./scripts/pre-commit/setup.sh
    default_user="$(docker image inspect "$(repo_checks_image_name "${SMOKE_REPO}")" --format '{{.Config.User}}')"
    [ "${default_user}" = "repo-checks" ] || {
        echo "Expected the repo-checks runtime image to default to a non-root user." >&2
        exit 1
    }
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
    repo_checks_run_image "${SMOKE_REPO}" git status --short > /dev/null
    repo_checks_run_image "${SMOKE_REPO}" python3 -c \
        'from pathlib import Path; import os; path = Path(os.environ["HOME"]) / "repo-checks-home-smoke"; path.write_text("ok"); print(path.read_text())' \
        > /dev/null

    temp_parent="${WORK_DIR}/-temp-parent"
    temp_file="$(repo_checks_create_temp_file "${temp_parent}")"
    [ -f "${temp_file}" ] || {
        echo "Expected repo_checks_create_temp_file to create a file under a dash-prefixed temp parent." >&2
        exit 1
    }
    temp_dir="$(repo_checks_create_temp_dir "${temp_parent}")"
    [ -d "${temp_dir}" ] || {
        echo "Expected repo_checks_create_temp_dir to create a directory under a dash-prefixed temp parent." >&2
        exit 1
    }

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
    [ ! -e "${custom_hooks_dir}" ] || {
        echo "Expected the custom hooks directory to be absent before setup." >&2
        exit 1
    }
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

    symlink_target="${WORK_DIR}/external-pre-commit"
    printf '%s\n' "external hook" > "${symlink_target}"
    rm -f -- "${custom_hooks_dir}/pre-commit"
    ln -s "${symlink_target}" "${custom_hooks_dir}/pre-commit"

    pushd "${launcher_dir}" > /dev/null
    if setup_output="$("${SMOKE_REPO}/scripts/pre-commit/setup.sh" 2>&1)"; then
        popd > /dev/null
        echo "Expected setup.sh to refuse overwriting a symlinked pre-commit hook." >&2
        exit 1
    fi
    popd > /dev/null

    printf '%s\n' "${setup_output}" | grep -Fq "Refusing to overwrite symlinked hook" || {
        echo "Expected setup.sh to explain why it refused the symlinked hook." >&2
        exit 1
    }
    grep -Fqx "external hook" "${symlink_target}" || {
        echo "Expected setup.sh not to modify the symlink target." >&2
        exit 1
    }

    popd > /dev/null
}

init_smoke_repo
run_smoke
