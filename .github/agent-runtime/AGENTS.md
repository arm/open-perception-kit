# AGENTS.md

This subtree owns Agent runtime assets: prompts, schemas, profiles, and
dependency pins. Executable GitHub Actions workflows live only under
`.github/workflows/`.

## Guardrails

- Keep this area Agent-branded. Do not introduce non-Agent workflow, model,
  action, or runner contracts.
- Keep prompt text and profile policy in checked-in files under
  `.github/agent-runtime/`; do not embed long prompts in workflow YAML or
  Python helpers.
- Keep concrete model names in
  `.github/agent-runtime/runtime/agent-models.json`. Profiles and workflow YAML
  should refer to the config path and agent instance instead of duplicating
  model strings.
- Keep OpenAI proxy defaults, task dispatch, model resolution, runtime setup,
  and output handling in `scripts/private/agent_runtime/` modules. Workflow YAML
  may call `setup_runtime.py` and `openai_agent_runner.py`, but must not
  duplicate venv setup, task-specific OpenAI logic, or turn/model defaults.
- Keep review recommendations, severity names, UI comment markers, and review
  false-positive guards in `scripts/private/agent_runtime/contracts.py`.
  Published PR comments are UI/log output only; `agent-review-out/review.json`
  is the only machine-readable review state contract.
- Delete obsolete Agent runtime code, stale tests, removed scripts, and old
  workflow entrypoints when replacing a path. Do not keep legacy aliases,
  compatibility wrappers, or duplicate implementations unless they are part of
  the current supported Agent runtime contract and have focused tests.
- Before reporting a GitHub Action ref as unavailable, verify it from current
  workflow logs or upstream tags. `actions/checkout@v6` and
  `actions/upload-artifact@v6` are valid in this workflow family.
- For Agent Review findings, a `RIGHT`-side finding must point at a file and
  line that exist in the current checkout. Do not treat deleted rename-side
  strings as current regressions.

## Validation

For changes here, run at least:

- `find scripts/private/agent_runtime scripts/private/workflow_action_update_agent -name '*.py' -print0 | xargs -0 python3 -m py_compile`
- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/tests`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s scripts/private/workflow_action_update_agent/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`

If a change touches dependency pins, review publication, stabilization, or the
OpenAI runner, also run the Agent Review workflow on the PR and inspect the
`agent-review-out/review.json` artifact. The PR summary and inline comments are
UI/log output, not a fallback machine-readable review database.
