# AGENTS.md

This directory owns prompt policy shared by source-run repair and current-PR
stabilization.

## Guardrails

- Keep policy generic enough for both flows. Flow-specific task details belong
  in `source-run-repair/` or `pr-stabilization/`.
- Do not include generated runtime data, logs, or review JSON here.
- If a policy file changes the allowed work surface, update both repair and
  stabilization prompt tests or contract tests as needed.

## Validation

For changes here, run at least:

- `PYTHONPATH=tools/expkits-ci python3 -m expkits_ci.agent_static_analysis`
- `python3 -m unittest discover -s scripts/private/agent_repair_orchestrator/tests`
- `python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`
