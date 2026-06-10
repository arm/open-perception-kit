#!/usr/bin/env bash

set -euo pipefail

repo_slug="${1:-}"
pull_number="${2:-}"

if [ -z "${repo_slug}" ] || [[ "${repo_slug}" != */* ]]; then
    echo 'Usage: scripts/ci/update-pr-body.sh <owner/repo> <pr-number>' >&2
    exit 2
fi

if [ -z "${pull_number}" ]; then
    echo 'Usage: scripts/ci/update-pr-body.sh <owner/repo> <pr-number>' >&2
    exit 2
fi

if [ -z "${GITHUB_TOKEN:-}" ]; then
    echo "Missing GITHUB_TOKEN." >&2
    exit 1
fi

body="$(cat)"
if [ -z "${body}" ]; then
    echo "PR description input from stdin is empty." >&2
    exit 1
fi

api_url="${GITHUB_API_URL:-https://api.github.com}"

curl --fail --silent --show-error \
    -X PATCH \
    -H "Accept: application/vnd.github+json" \
    -H "Authorization: Bearer ${GITHUB_TOKEN}" \
    -H "User-Agent: amp-dev-forge-pr-bookkeeping" \
    -H "Content-Type: application/json" \
    "${api_url}/repos/${repo_slug}/pulls/${pull_number}" \
    --data "$(printf '%s' "${body}" | jq -Rs '{body: .}')"
