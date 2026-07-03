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
- Keep profile paths, labels, validation workflow names, and prompt context
  files aligned with `scripts/private/workflow_action_update_agent/` and
  `.github/actions/workflow-action-update-agent-helper/action.yml`.
- Treat generated `.agent-runtime/workflow-action-update-agent/` files as
  runtime artifacts. Do not check generated context, logs, artifacts, or agent
  outputs into this directory.
- When moving or renaming a prompt/profile path, update workflow inputs,
  helper defaults, docs, and stale-reference tests in the same change.

## Validation

For changes here, run at least:

- `python3 scripts/private/agent_runtime/static_analysis.py`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --check`
