# AGENTS.md

This directory owns the shared Python OpenAI Agents SDK runtime used by the
review, repair, and stabilization GitHub Actions workflows.

## Guardrails

- Keep reusable contracts in `contracts.py`; do not duplicate OpenAI proxy env
  names, recommendations, severities, diff sides, or review false-positive
  guards in individual modules.
- Define the shared task interface in `workflow_task.py`. User-facing workflow
  agents should extend `ConfiguredAgentWorkflowTask` in `agent_tasks.py` and
  register in `openai_agent_runner.py`; keep the matching central task settings
  in `.github/agent-runtime/runtime/agent-tasks.json`.
- Resolve agent models through `model_config.py` and the checked-in
  `.github/agent-runtime/runtime/agent-models.json` file. Do not introduce
  per-workflow-YAML hardcoded model names.
- Keep model/task/profile config validation helpers in `contracts.py`; do not
  duplicate JSON object, enum, string, list, or positive integer checks in
  individual config readers.
- Keep OpenAI proxy defaults, task dispatch, model resolution, and output
  handling in the Python runtime modules. Workflow YAML and local scripts may
  install the venv and call `openai_agent_runner.py`, but must not duplicate
  task-specific OpenAI logic or model/turn defaults.
- Keep `truststore.inject_into_ssl()` before importing `agents`, `openai`, or
  `httpx` through the SDK stack.
- Agent tools may inspect files and run validation, but must not own branch,
  commit, push, PR, or merge lifecycle. Those steps belong to the surrounding
  workflow/helper.
- Delete obsolete runtime code, stale tests, removed scripts, and compatibility
  wrappers when replacing behavior. Do not leave legacy aliases or duplicate
  implementations outside the current supported contract.
- Do not execute agent-provided commands with `shell=True`. Keep validation
  commands tokenized and reject unsupported shell syntax, mutating git
  subcommands, and PR/repo lifecycle `gh` subcommands.
- Review output post-processing must only drop findings contradicted by current
  checkout evidence or verified workflow/action evidence. Keep those rules
  typed and covered by tests.

## Adding a Workflow Agent

1. Add one `ConfiguredAgentWorkflowTask` subclass in `agent_tasks.py` or a
   focused module imported by that file. The subclass owns task-specific CLI
   arguments, validation, tools, output type, and result writing.
2. Add exactly one entry for the command to `AGENT_TASKS` in
   `openai_agent_runner.py`.
3. Add the same command to `.github/agent-runtime/runtime/agent-tasks.json`
   with its agent instance and limits.
4. Wire workflow YAML or local scripts to call
   `openai_agent_runner.py <command>` and pass only prompt/output/config paths.
   Do not duplicate model names, max-turn values, or agent instances in YAML.
5. Extend `tools/expkits-ci/tests/test_workflow_action_update_agent_flow.py`
   so the new command is covered by the central registry/config enforcement.

The generic preflight size estimator lives in `task_estimator.py` as a
`TaskEstimatorWorkflowTask`; it should not be copied into individual workflow
tasks.

## Validation

For changes here, run at least:

- `find scripts/private/agent_runtime -name '*.py' -print0 | xargs -0 python3 -m py_compile scripts/private/workflow_action_update_agent.py`
- `python3 scripts/private/agent_runtime/static_analysis.py`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --check`
