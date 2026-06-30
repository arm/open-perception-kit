#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

base_ref="${1:-origin/main}"
output_dir="${2:-codex-review/out}"

mkdir -p "${output_dir}"

REVIEW_BASE_REF="${base_ref}" \
    REVIEW_HEAD_REF="${REVIEW_HEAD_REF:-HEAD}" \
    REVIEW_REPOSITORY="${REVIEW_REPOSITORY:-local-checkout}" \
    ./codex-review/scripts/render-prompt.sh "${output_dir}/review.prompt.md"

python3 -m pip install --user -r codex-review/requirements-agent.txt

python3 scripts/private/openai_agent_runner.py run-review \
    --model "${CODEX_MODEL:-gpt-5.3-codex}" \
    --prompt-file "${output_dir}/review.prompt.md" \
    --schema-file "codex-review/schemas/review.schema.json" \
    --output-file "${output_dir}/review.json"

python3 ./codex-review/scripts/publish-review.py \
    --input "${output_dir}/review.json" \
    --markdown-out "${output_dir}/review-summary.md"

echo "Prompt: ${output_dir}/review.prompt.md"
echo "Review JSON: ${output_dir}/review.json"
echo "Review summary: ${output_dir}/review-summary.md"
