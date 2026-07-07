# AGENTS.md

This directory owns current-PR stabilization profiles and prompt templates.

## Guardrails

- Keep this root specific to fixing the latest standard Agent Review findings
  on an existing PR branch.
- Stabilization profiles keep only display name, model/task config paths,
  prompt context files, and a validation command set. Do not add repair branch,
  PR-generation, or label metadata here.
- Stabilization consumes `agent-review-out/review.json` as machine-readable
  review state. Do not rely on PR comments, workflow conclusions, annotations,
  or logs as the stabilization contract.
- Treat generated `.agent-runtime/pr-stabilization/` files as runtime
  artifacts. Do not check generated context or agent outputs into this
  directory.

## Validation

For changes here, run at least:

- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`
