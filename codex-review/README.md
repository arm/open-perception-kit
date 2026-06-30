# Codex Review

This directory owns the repository-specific Codex review flow.

The GitHub workflow renders the review prompt, runs the shared Python OpenAI
Agents SDK runner, then
publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings
- structured hidden state markers in the published comments, so other automation
  can safely consume the latest Codex review result for the current PR head

The workflow-scoped OpenAI agent runtime pins live in
`codex-review/requirements-agent.txt` and are installed into
`.codex/openai-agent-venv`. The runner uses the Arm OpenAI proxy, disables
Agents SDK tracing, and injects `truststore` before importing OpenAI libraries.

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Codex review runs
- `scripts/`: shared helper scripts for prompt rendering, local review runs, and
  publishing review output
- `requirements-agent.txt`: pinned OpenAI agent runtime dependencies
- `out/`: local and CI-generated review artifacts
