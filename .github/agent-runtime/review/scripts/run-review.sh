#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

base_ref="${1:-origin/main}"
output_dir="${2:-.github/agent-runtime/review/out}"

mkdir -p "${output_dir}"

export REVIEW_BASE_REF="${base_ref}"
export REVIEW_HEAD_REF="${REVIEW_HEAD_REF:-HEAD}"
export REVIEW_REPOSITORY="${REVIEW_REPOSITORY:-local-checkout}"

./.github/agent-runtime/review/scripts/render-prompt.sh "${output_dir}/review.prompt.md"

agent_venv="${AGENT_REVIEW_AGENT_VENV:-.agent-runtime/openai-agent-venv}"
agent_python="${AGENT_REVIEW_PYTHON:-${AGENT_RUNTIME_PYTHON:-python3}}"

if ! command -v "${agent_python}" > /dev/null 2>&1; then
    echo "Agent review requires Python 3.10 or newer; '${agent_python}' was not found." >&2
    exit 1
fi

"${agent_python}" - <<'PY'
import sys

if sys.version_info < (3, 10):
    version = ".".join(str(part) for part in sys.version_info[:3])
    raise SystemExit(f"Agent review requires Python 3.10 or newer; found Python {version}.")
PY

"${agent_python}" -m venv "${agent_venv}"
"${agent_venv}/bin/python" -m pip install --upgrade pip
"${agent_venv}/bin/python" -m pip install -r .github/agent-runtime/runtime/requirements-openai-agents.txt

agent_args=(
    run-review
    --prompt-file "${output_dir}/review.prompt.md"
    --schema-file ".github/agent-runtime/review/schemas/review.schema.json"
    --output-file "${output_dir}/review.json"
)
if [[ -n "${AGENT_REVIEW_MODEL:-}" ]]; then
    agent_args+=(--model "${AGENT_REVIEW_MODEL}")
fi

"${agent_venv}/bin/python" scripts/private/agent_runtime/openai_agent_runner.py "${agent_args[@]}"

"${agent_venv}/bin/python" ./.github/agent-runtime/review/scripts/publish-review.py \
    --input "${output_dir}/review.json" \
    --markdown-out "${output_dir}/review-summary.md"

echo "Prompt: ${output_dir}/review.prompt.md"
echo "Review JSON: ${output_dir}/review.json"
echo "Review summary: ${output_dir}/review-summary.md"
