# AGENTS.md

This directory owns the checked-in profiles and Markdown prompt templates for
the Workflow Action Update Agent repair and stabilization flows.

## Guardrails

- Keep long prompt and policy text here, not in workflow YAML or Python helper
  modules.
- Profiles should reference model and task settings through
  `.github/agent-runtime/runtime/agent-models.json` and
  `.github/agent-runtime/runtime/agent-tasks.json`; do not hardcode concrete
  model names or turn limits in profiles or workflows.
- Profiles may select validation command set names and labels from
  `scripts/private/workflow_action_update_agent/runtime.py`; do not inline
  workflow files, dispatch inputs, review-state scripts, or validation command
  lists in profiles.
- Keep public manual stabilization inputs on
  `.github/workflows/workflow-action-update-agent.yml`; profiles must not
  point at the callable stabilizer worker or create a second public stabilizer
  entrypoint.
- Treat generated `.agent-runtime/workflow-action-update-agent/` files as
  runtime artifacts. Do not check generated context, logs, artifacts, or agent
  outputs into this directory.
- When moving or renaming a prompt/profile path, update workflow inputs,
  helper defaults, docs, and stale-reference tests in the same change.

## Validation

For changes here, run at least:

- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/tests`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s scripts/private/workflow_action_update_agent/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`
