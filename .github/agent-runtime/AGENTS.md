# AGENTS.md

This subtree owns Agent runtime assets: static review instructions, repair and
stabilization profiles, shared workflow policy prompts, and dependency pins.
Executable GitHub Actions workflows live only under `.github/workflows/`.

## Guardrails

- Keep this area Agent-branded. Do not introduce non-Agent workflow, model,
  action, or runner contracts.
- Keep Agent instructions, prompt text, and profile policy in checked-in files under
  `.github/agent-runtime/`; do not embed long prompts in workflow YAML or
  Python helpers.
- Keep OpenAI proxy defaults, task dispatch, model resolution, runtime setup,
  and output handling in `scripts/private/agent_runtime/` modules. Workflow YAML
  may call `setup_runtime.py` and `openai_agent_runner.py`, but must not
  duplicate venv setup, task-specific OpenAI logic, or turn/model defaults.
- Keep repair profiles and prompts under
  `.github/agent-runtime/source-run-repair/`.
- Keep current-PR stabilization profiles and prompts under
  `.github/agent-runtime/pr-stabilization/`.
- Keep shared prompt policy under `.github/agent-runtime/workflow-policy/`.
- Keep review recommendations, severity names, UI comment markers, and review
  false-positive guards in `scripts/private/agent_runtime/contracts.py`.
  Published PR comments are UI/log output only; `agent-review-out/review.json`
  is the only machine-readable review state contract.
- Delete obsolete Agent runtime code, stale tests, removed scripts, and old
  workflow entrypoints when replacing a path. Do not keep legacy aliases,
  compatibility wrappers, or duplicate implementations unless they are part of
  the current supported Agent runtime contract and have focused tests.

## Validation

For changes here, run at least:

- `find scripts/private/agent_runtime scripts/private/agent_repair_orchestrator scripts/private/agent_stabilization_orchestrator scripts/private/agent_workflow_common -name '*.py' -print0 | xargs -0 python3 -m py_compile`
- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/tests`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s scripts/private/agent_repair_orchestrator/tests`
- `python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`

If a change touches dependency pins, review publication, stabilization, or the
OpenAI runner, also run the Agent Review workflow on the PR and inspect the
`agent-review-out/review.json` artifact. The PR summary and inline comments are
UI/log output, not a fallback machine-readable review database.
