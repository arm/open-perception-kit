#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

base_ref="${1:-origin/main}"
output_dir="${2:-.github/agent-workflows/review/out}"

mkdir -p "${output_dir}"

export REVIEW_BASE_REF="${base_ref}"
export REVIEW_HEAD_REF="${REVIEW_HEAD_REF:-HEAD}"
export REVIEW_REPOSITORY="${REVIEW_REPOSITORY:-local-checkout}"

./.github/agent-workflows/review/scripts/render-prompt.sh "${output_dir}/review.prompt.md"

agent_venv="${AGENT_REVIEW_AGENT_VENV:-.agent-workflows/openai-agent-venv}"

if [[ -z "${OPENAI_API_KEY:-}" && -n "${OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS:-}" ]]; then
    export OPENAI_API_KEY="${OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS}"
fi

if [[ -z "${OPENAI_API_KEY:-}" ]]; then
    cat >&2 << 'EOF'
OpenAI SDK review requires OPENAI_API_KEY or OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS.
For the Arm OpenAI proxy, run for example:
  export OPENAI_BASE_URL="https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1"
  export OPENAI_AGENTS_DISABLE_TRACING=1
  export OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS="<proxy key>"
EOF
    exit 2
fi

python3 -m venv "${agent_venv}"
"${agent_venv}/bin/python" -m pip install --upgrade pip
"${agent_venv}/bin/python" -m pip install -r .github/agent-workflows/runtime/requirements-openai-agents.txt

"${agent_venv}/bin/python" scripts/private/agent_workflows/openai_agent_runner.py run-review \
    --model "${AGENT_MODEL:-gpt-5.3-codex}" \
    --prompt-file "${output_dir}/review.prompt.md" \
    --schema-file ".github/agent-workflows/review/schemas/review.schema.json" \
    --output-file "${output_dir}/review.json"

python3 ./.github/agent-workflows/review/scripts/publish-review.py \
    --input "${output_dir}/review.json" \
    --markdown-out "${output_dir}/review-summary.md"

echo "Prompt: ${output_dir}/review.prompt.md"
echo "Review JSON: ${output_dir}/review.json"
echo "Review summary: ${output_dir}/review-summary.md"
