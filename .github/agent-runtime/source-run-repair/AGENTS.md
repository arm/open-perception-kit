# AGENTS.md

This directory owns source-run repair profiles and prompt templates.

## Guardrails

- Keep this root specific to repair work that creates a separate draft PR from
  source-run logs and artifacts.
- Keep branch, PR, label, DoD, and repair metadata templates in repair profiles.
  Do not add current-PR stabilization settings here.
- Profiles should reference model and task settings through
  `.github/agent-runtime/runtime/agent-models.json` and
  `.github/agent-runtime/runtime/agent-tasks.json`; do not hardcode concrete
  model names or turn limits in profiles or workflows.
- Profiles may select validation command set names and labels from the checked
  Python helpers; do not inline workflow files, review-state scripts, or
  validation command lists in profiles.
- Treat generated `.agent-runtime/source-run-repair/` files as runtime
  artifacts. Do not check generated context, logs, artifacts, or agent outputs
  into this directory.
- When moving or renaming a prompt/profile path, update workflow inputs,
  helper defaults, docs, and stale-reference tests in the same change.

## Validation

For changes here, run at least:

- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/tests`
- `python3 -m unittest discover -s scripts/private/agent_runtime/tests`
- `python3 -m unittest discover -s scripts/private/agent_repair_orchestrator/tests`
- `python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`
