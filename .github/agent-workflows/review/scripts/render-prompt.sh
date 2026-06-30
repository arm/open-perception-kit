#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <output-path> [template-path]" >&2
    exit 2
fi

output_path="$1"
template_path="${2:-.github/agent-workflows/review/prompts/review.md.in}"

base_ref="${REVIEW_BASE_REF:-origin/main}"
head_ref="${REVIEW_HEAD_REF:-HEAD}"
base_sha="${REVIEW_BASE_SHA:-}"
head_sha="${REVIEW_HEAD_SHA:-}"
repository="${REVIEW_REPOSITORY:-${GITHUB_REPOSITORY:-$(basename "$(git rev-parse --show-toplevel)")}}"
pr_number="${REVIEW_PR_NUMBER:-}"
pr_title="${REVIEW_PR_TITLE:-}"
pr_url="${REVIEW_PR_URL:-}"

if [ -z "${head_sha}" ]; then
    head_sha="$(git rev-parse "${head_ref}")"
fi

if [ -z "${base_sha}" ]; then
    base_sha="$(git merge-base "${base_ref}" "${head_ref}")"
fi

mkdir -p "$(dirname "${output_path}")"

escape_sed_replacement() {
    printf '%s' "$1" | sed -e 's/[&|\\]/\\&/g'
}

normalize_text() {
    printf '%s' "$1" | tr '\n' ' '
}

fill_empty() {
    local value="$1"
    local fallback="$2"

    if [ -n "${value}" ]; then
        printf '%s' "${value}"
    else
        printf '%s' "${fallback}"
    fi
}

repository="$(fill_empty "${repository}" "(not provided)")"
base_ref="$(fill_empty "${base_ref}" "(not provided)")"
base_sha="$(fill_empty "${base_sha}" "(not provided)")"
head_sha="$(fill_empty "${head_sha}" "(not provided)")"
pr_number="$(fill_empty "${pr_number}" "(not a pull request run)")"
pr_title="$(fill_empty "$(normalize_text "${pr_title}")" "(not provided)")"
pr_url="$(fill_empty "${pr_url}" "(not provided)")"

sed \
    -e "s|@@REPOSITORY@@|$(escape_sed_replacement "${repository}")|g" \
    -e "s|@@BASE_REF@@|$(escape_sed_replacement "${base_ref}")|g" \
    -e "s|@@BASE_SHA@@|$(escape_sed_replacement "${base_sha}")|g" \
    -e "s|@@HEAD_SHA@@|$(escape_sed_replacement "${head_sha}")|g" \
    -e "s|@@PR_NUMBER@@|$(escape_sed_replacement "${pr_number}")|g" \
    -e "s|@@PR_TITLE@@|$(escape_sed_replacement "${pr_title}")|g" \
    -e "s|@@PR_URL@@|$(escape_sed_replacement "${pr_url}")|g" \
    "${template_path}" > "${output_path}"
