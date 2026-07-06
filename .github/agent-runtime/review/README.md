# Agent Review

This directory owns the repository-specific Agent review flow.

The GitHub workflow calls the shared Python Agent runtime to render the review
prompt, runs the OpenAI Agents SDK runner, then
publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings
- the `agent-review-out/review.json` artifact as the canonical
  machine-readable review state for the current PR head
- UI-only comment markers that identify Agent Review comments without storing
  machine-readable review state in PR comments

The Agent Review workflow does not run on pull request label changes. The
`agent-stabilize` label is handled by a separate label-triggered workflow that
calls the shared stabilizer from the latest canonical review artifact, so
unrelated labels do not create or overwrite Agent Review gate checks.

The workflow-scoped OpenAI agent runtime pins live in
`.github/agent-runtime/runtime/requirements-openai-agents.txt` and are installed into
`.agent-runtime/openai-agent-venv` by
`../../../scripts/private/agent_runtime/setup_runtime.py`. The runner uses the
Arm OpenAI proxy, disables Agents SDK tracing, and injects `truststore` before
importing OpenAI libraries.
Local runs use the same SDK path and require either `OPENAI_API_KEY` or
`OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS`; OpenAI SDK CLI login state is not reused.

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Agent review runs
- `../../../scripts/private/agent_runtime/`: shared helper modules for runtime
  setup, prompt rendering, local review runs, artifact-state fetching, and
  publishing review output
- `../runtime/requirements-openai-agents.txt`: pinned OpenAI agent runtime dependencies
- `out/`: local and CI-generated review artifacts
