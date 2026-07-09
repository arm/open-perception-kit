# Agent Review

This directory owns the repository-specific Agent review flow.

The GitHub workflow builds a bounded structured review context, calls the shared
Python OpenAI Agents SDK runtime with that typed context, then publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings
- the `agent-review-out/review.json` artifact as the canonical
  machine-readable review state for the current PR head
- the structured `agent-review-out/review-context.json` artifact used for the
  run, containing no raw pull request body
- UI-only comment markers that identify Agent Review comments without storing
  machine-readable review state in PR comments

During the SDK run, the context artifact and the GitHub Actions event payload
are temporarily removed from the filesystem and restored afterward. This keeps
model-visible review metadata on the dedicated `get_review_context` tool
boundary, prevents shell-tool access to the workflow-provided copy of the raw
PR body, and still retains the context artifact for auditability. Review shell
commands also run without GitHub, OpenAI, token, key, or credential environment
variables and use an isolated home directory; the existing read, build, and
validation command surface remains unchanged.

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

- `instructions.md`: static trusted review policy loaded directly into the SDK
  `Agent`; it contains no pull request values or runtime placeholders
- `../../../scripts/private/agent_runtime/`: shared helper modules for runtime
  setup, context generation, local review runs, artifact-state fetching, and
  publishing review output
- `../runtime/requirements-openai-agents.txt`: pinned OpenAI agent runtime dependencies
- `out/`: local and CI-generated review artifacts
