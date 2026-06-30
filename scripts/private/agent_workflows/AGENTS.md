# AGENTS.md

This directory owns the shared Python OpenAI Agents SDK runtime used by review,
repair, and stabilization workflows.

## Guardrails

- Keep reusable contracts in `contracts.py`; do not duplicate OpenAI proxy env
  names, recommendations, severities, diff sides, or review false-positive
  guards in individual modules.
- Resolve agent models through `model_config.py` and the checked-in
  `.github/agent-workflows/runtime/agent-models.json` file. Do not introduce
  per-workflow hardcoded model names.
- Keep `truststore.inject_into_ssl()` before importing `agents`, `openai`, or
  `httpx` through the SDK stack.
- Agent tools may inspect files and run validation, but must not own branch,
  commit, push, PR, or merge lifecycle. Those steps belong to the surrounding
  workflow/helper.
- Do not execute agent-provided commands with `shell=True`. Keep validation
  commands tokenized and reject unsupported shell syntax, mutating git
  subcommands, and PR/repo lifecycle `gh` subcommands.
- Review output post-processing must only drop findings contradicted by current
  checkout evidence or verified workflow/action evidence. Keep those rules
  typed and covered by tests.

## Validation

For changes here, run at least:

- `python3 -m py_compile scripts/private/agent_workflows/*.py scripts/private/workflow_action_update_agent.py`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --check`
