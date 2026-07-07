# AGENTS.md

This package owns shared helper code used by source-run repair and current-PR
stabilization orchestration packages.

## Guardrails

- Keep only cross-flow helpers here: subprocess execution, GitHub output, JSON
  file writing, branch push helpers, Agent Review workflow metadata, task-ref
  resolution, shared profile runtime-config helpers, and canonical validation
  command sets.
- Do not add flow-specific profile schemas, prompt templates, source-run repair
  logic, or stabilization commit logic here.
- Keep validation command sets synchronized with `AGENTS.md` validation blocks
  and `tools/expkits-ci/tests/test_agent_workflow_contracts.py`.

## Validation

For changes here, run at least:

- `find scripts/private/agent_workflow_common scripts/private/agent_repair_orchestrator scripts/private/agent_stabilization_orchestrator -name '*.py' -print0 | xargs -0 python3 -m py_compile`
- `python3 -m unittest discover -s scripts/private/agent_repair_orchestrator/tests`
- `python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests`
- `PYTHONPATH=tools/expkits-ci python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'`
- `git diff --check`
