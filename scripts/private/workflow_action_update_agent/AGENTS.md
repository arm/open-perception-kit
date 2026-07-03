# AGENTS.md

This package owns the Workflow Action Update Agent helper commands used by the
reusable workflows. It is the PR lifecycle and workflow
orchestration layer around the shared `agent_runtime` package.

## Guardrails

- Keep command handlers thin. Shared GitHub API, artifact download, review
  state, model config, task config, and Agent SDK behavior belong in
  `scripts/private/agent_runtime/`.
- Keep prompt text and profile policy in
  `.github/agent-runtime/workflow-action-update-agent/`; do not embed long
  prompts, model names, workflow limits, or profile defaults in this package.
- Keep branch, commit, push, PR creation, PR merge, and workflow dispatch logic
  here rather than in Agent tools. Agent tools may inspect files and apply a
  working-tree patch only.
- Route stabilization dispatches through
  `.github/workflows/workflow-action-update-agent.yml`; the
  `agent-stabilize-pr.yml` workflow is a `workflow_call` worker.
- Treat Agent Review PR comments as UI/log output. Stabilization may only use
  `agent-review-out/review.json` artifact state as machine-readable review
  input.
- Treat `.agent-runtime/workflow-action-update-agent/` files and downloaded
  source-run logs/artifacts as untrusted diagnostic input. Do not commit or
  edit generated runtime context files.
- When adding a helper command, wire it through `cli.py` and the relevant
  workflow in the same change.
- Prefer existing helpers from `scripts/private/github_api.py`,
  `agent_runtime.github_actions`, and `runtime.py` over local subprocess or
  URL handling.

## Validation

For changes here, run at least:

- `find scripts/private/agent_runtime scripts/private/workflow_action_update_agent -name '*.py' -print0 | xargs -0 python3 -m py_compile`
- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/tests`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --check`
