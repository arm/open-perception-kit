# AGENTS.md

This subtree owns the Agent workflow prompts, schemas, profiles, and runtime
dependency pins. Keep workflow behavior stable before changing wording or file
layout.

## Guardrails

- Keep this area Agent-branded. Do not reintroduce `codex-review`,
  `codex-stabilize`, `codex_model`, `CODEX_REVIEW`, `openai/codex-action@v1`,
  or `codex exec` contracts.
- Keep prompt text and profile policy in checked-in files under
  `.github/agent-workflows/`; do not embed long prompts in workflow YAML or
  Python helpers.
- Keep concrete model names in
  `.github/agent-workflows/runtime/agent-models.json`. Profiles and workflows
  should refer to the config path and agent instance instead of duplicating
  model strings.
- Keep review markers, recommendation names, severity names, author defaults,
  and GitHub API identity in `.github/agent-workflows/review/scripts/review_contract.py`.
  Publish and fetch scripts must import that contract instead of duplicating
  marker strings.
- Before reporting a GitHub Action ref as unavailable, verify it from current
  workflow logs or upstream tags. `actions/checkout@v6` and
  `actions/upload-artifact@v6` are valid in this workflow family.
- For Agent Review findings, a `RIGHT`-side finding must point at a file and
  line that exist in the current checkout. Do not treat deleted rename-side
  strings as current regressions.

## Validation

For changes here, run at least:

- `python3 -m py_compile scripts/private/agent_workflows/*.py scripts/private/workflow_action_update_agent.py .github/agent-workflows/review/scripts/*.py`
- `python3 -m mypy --config-file .github/agent-workflows/runtime/mypy.ini`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --check`

If a change touches dependency pins, review publication, stabilization, or the
OpenAI runner, also run the Agent Review workflow on the PR and inspect the
`agent-review-out/review.json` artifact.
