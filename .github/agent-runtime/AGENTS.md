# AGENTS.md

This subtree owns Agent runtime assets: prompts, schemas, profiles, and
dependency pins. Executable GitHub Actions workflows live only under
`.github/workflows/`.

## Guardrails

- Keep this area Agent-branded. Do not reintroduce `codex-review`,
  `codex-stabilize`, `codex_model`, `CODEX_REVIEW`, `openai/codex-action@v1`,
  or `codex exec` contracts.
- Keep prompt text and profile policy in checked-in files under
  `.github/agent-runtime/`; do not embed long prompts in workflow YAML or
  Python helpers.
- Keep concrete model names in
  `.github/agent-runtime/runtime/agent-models.json`. Profiles and workflow YAML
  should refer to the config path and agent instance instead of duplicating
  model strings.
- Keep OpenAI proxy defaults, task dispatch, model resolution, and output
  handling in `scripts/private/agent_runtime/openai_agent_runner.py` and its
  Python modules. Workflow YAML may create the venv and call that runner, but
  must not duplicate task-specific OpenAI logic or turn/model defaults.
- Keep review markers, recommendation names, severity names, author defaults,
  and GitHub API identity in `.github/agent-runtime/review/scripts/review_contract.py`.
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

- `find scripts/private/agent_runtime .github/agent-runtime/review/scripts -name '*.py' -print0 | xargs -0 python3 -m py_compile scripts/private/workflow_action_update_agent.py`
- `python3 -m mypy --config-file .github/agent-runtime/runtime/mypy.ini`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --check`

If a change touches dependency pins, review publication, stabilization, or the
OpenAI runner, also run the Agent Review workflow on the PR and inspect the
`agent-review-out/review.json` artifact.
