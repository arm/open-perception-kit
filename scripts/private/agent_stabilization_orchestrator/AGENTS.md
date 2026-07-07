# AGENTS.md

This package owns current-PR stabilization helper commands used by
`.github/workflows/agent-stabilize-pr-worker.yml`.

## Guardrails

- Keep command handlers thin. Shared process, GitHub output, branch push,
  review workflow metadata, validation, and task-ref helpers belong in
  `scripts/private/agent_workflow_common/`.
- Keep OpenAI SDK behavior in `scripts/private/agent_runtime/`.
- Keep stabilization prompt text and profile policy in
  `.github/agent-runtime/pr-stabilization/`.
- Stabilization may only consume canonical Agent Review state from
  `agent-review-out/review.json`. PR comments are UI/log output.
- Do not add source-run repair, draft PR creation, or repair-branch template
  handling here.
- Keep helper snapshots under `.agent-runtime/agent-stabilization-helper/`.

## Validation

For changes here, run at least:

- `find scripts/private/agent_stabilization_orchestrator scripts/private/agent_workflow_common -name '*.py' -print0 | xargs -0 python3 -m py_compile`
- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`
