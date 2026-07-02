# Agent Review

This directory owns the repository-specific Agent review flow.

The GitHub workflow calls the shared Python Agent runtime to render the review
prompt, runs the OpenAI Agents SDK runner, then
publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings
- structured hidden state markers in the published comments, so other automation
  can safely consume the latest Agent review result for the current PR head

The workflow-scoped OpenAI agent runtime pins live in
`.github/agent-runtime/runtime/requirements-openai-agents.txt` and are installed into
`.agent-runtime/openai-agent-venv`. The runner uses the Arm OpenAI proxy, disables
Agents SDK tracing, and injects `truststore` before importing OpenAI libraries.
Local runs use the same SDK path and require either `OPENAI_API_KEY` or
`OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS`; OpenAI SDK CLI login state is not reused.

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Agent review runs
- `../../../scripts/private/agent_runtime/`: shared helper modules for prompt
  rendering, local review runs, review-state fetching, and publishing review
  output
- `../runtime/requirements-openai-agents.txt`: pinned OpenAI agent runtime dependencies
- `out/`: local and CI-generated review artifacts
