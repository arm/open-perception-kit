#!/usr/bin/env bash

set -euo pipefail

if [ -z "${COPILOT_GITHUB_TOKEN:-}" ]; then
    echo "Missing COPILOT_GITHUB_TOKEN." >&2
    echo "Create a fine-grained PAT with the Copilot Requests permission and export it as COPILOT_GITHUB_TOKEN." >&2
    exit 1
fi

if [ -z "${PR_TARGET_BRANCH:-}" ]; then
    echo "Missing PR_TARGET_BRANCH." >&2
    exit 1
fi

if [ -z "${PR_SOURCE_BRANCH:-}" ]; then
    echo "Missing PR_SOURCE_BRANCH." >&2
    exit 1
fi

if [ -z "${PR_HEAD_REPO_URL:-}" ]; then
    echo "Missing PR_HEAD_REPO_URL." >&2
    exit 1
fi

copilot -p "$(cat .github/instructions/pr.instructions.md)" \
    --allow-tool='shell(git:*)' \
    --allow-tool='shell(cat:*)' \
    --no-ask-user
