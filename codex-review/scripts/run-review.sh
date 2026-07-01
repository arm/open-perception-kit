#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

if ! command -v codex > /dev/null 2>&1; then
    echo "codex CLI is required in PATH." >&2
    exit 1
fi

base_ref="${1:-origin/main}"
output_dir="${2:-codex-review/out}"

mkdir -p "${output_dir}"

REVIEW_BASE_REF="${base_ref}" \
    REVIEW_HEAD_REF="${REVIEW_HEAD_REF:-HEAD}" \
    REVIEW_REPOSITORY="${REVIEW_REPOSITORY:-local-checkout}" \
    ./codex-review/scripts/render-prompt.sh "${output_dir}/review.prompt.md"

codex exec \
    --model "${CODEX_MODEL:-gpt-5.3-codex}" \
    --sandbox danger-full-access \
    --output-schema "codex-review/schemas/review.schema.json" \
    --output-last-message "${output_dir}/review.json" \
    < "${output_dir}/review.prompt.md"

python3 ./codex-review/scripts/publish-review.py \
    --input "${output_dir}/review.json" \
    --markdown-out "${output_dir}/review-summary.md"

echo "Prompt: ${output_dir}/review.prompt.md"
echo "Review JSON: ${output_dir}/review.json"
echo "Review summary: ${output_dir}/review-summary.md"
