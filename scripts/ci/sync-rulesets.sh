#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

repo_slug="${1:-${GITHUB_REPOSITORY:-}}"
ruleset_dir="${2:-.github/rulesets}"
api_url="${GITHUB_API_URL:-https://api.github.com}"
api_version="${GITHUB_API_VERSION:-2022-11-28}"
token="${RULESET_ADMIN_GITHUB_TOKEN:-}"

if [ -z "${repo_slug}" ] || [[ "${repo_slug}" != */* ]]; then
    echo "Usage: scripts/ci/sync-rulesets.sh <owner/repo> [ruleset-dir]" >&2
    exit 2
fi

if [ ! -d "${ruleset_dir}" ]; then
    echo "Ruleset directory not found: ${ruleset_dir}" >&2
    exit 1
fi

if [ -z "${token}" ]; then
    echo "Missing RULESET_ADMIN_GITHUB_TOKEN." >&2
    echo "No fallback to GITHUB_TOKEN is supported for ruleset administration." >&2
    echo "Use a GitHub App installation token or fine-grained PAT with Administration: write on the repository." >&2
    exit 1
fi

if ! command -v curl > /dev/null 2>&1; then
    echo "curl is required." >&2
    exit 1
fi

if ! command -v jq > /dev/null 2>&1; then
    echo "jq is required." >&2
    exit 1
fi

mapfile -t ruleset_files < <(find "${ruleset_dir}" -maxdepth 1 -type f -name '*.json' | sort)

if [ "${#ruleset_files[@]}" -eq 0 ]; then
    echo "No ruleset JSON files found in ${ruleset_dir}." >&2
    exit 1
fi

auth_headers=(
    -H "Accept: application/vnd.github+json"
    -H "Authorization: Bearer ${token}"
    -H "User-Agent: amp-dev-forge-ruleset-sync"
    -H "X-GitHub-Api-Version: ${api_version}"
)

fetch_rulesets() {
    curl --fail --silent --show-error \
        "${auth_headers[@]}" \
        "${api_url}/repos/${repo_slug}/rulesets?includes_parents=false&per_page=100"
}

existing_rulesets="$(fetch_rulesets)"

for ruleset_file in "${ruleset_files[@]}"; do
    jq empty "${ruleset_file}"

    ruleset_name="$(jq -r '.name' "${ruleset_file}")"
    ruleset_target="$(jq -r '.target // "branch"' "${ruleset_file}")"
    ruleset_payload="$(jq -c . "${ruleset_file}")"

    if [ -z "${ruleset_name}" ] || [ "${ruleset_name}" = "null" ]; then
        echo "Ruleset file is missing a name: ${ruleset_file}" >&2
        exit 1
    fi

    existing_id="$(
        jq -r \
            --arg name "${ruleset_name}" \
            --arg target "${ruleset_target}" \
            '.[]
            | select(.source_type == "Repository")
            | select(.name == $name and .target == $target)
            | (.id // empty)' \
            <<< "${existing_rulesets}" |
              head -n 1
    )"

    if [ -n "${existing_id}" ]; then
        echo "Updating ruleset '${ruleset_name}' (${existing_id}) from ${ruleset_file}"
        curl --fail --silent --show-error \
            -X PUT \
            "${auth_headers[@]}" \
            -H "Content-Type: application/json" \
            "${api_url}/repos/${repo_slug}/rulesets/${existing_id}" \
            --data "${ruleset_payload}" \
            > /dev/null
    else
        echo "Creating ruleset '${ruleset_name}' from ${ruleset_file}"
        curl --fail --silent --show-error \
            -X POST \
            "${auth_headers[@]}" \
            -H "Content-Type: application/json" \
            "${api_url}/repos/${repo_slug}/rulesets" \
            --data "${ruleset_payload}" \
            > /dev/null
    fi

    existing_rulesets="$(fetch_rulesets)"
done
