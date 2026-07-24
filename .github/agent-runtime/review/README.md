# Agent Review

This directory owns the repository-specific Agent review flow.

The GitHub workflow builds a bounded structured review context, calls the shared
Python OpenAI Agents SDK runtime with that typed context, then publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings
- the `agent-review-out/review.json` artifact as the canonical
  machine-readable review state for the current PR head
- the structured `agent-review-out/review-context.json` artifact used for the
  run, containing bounded basic pull request fields without assuming a body
  template or extracting structural intent
- UI-only comment markers that identify Agent Review comments without storing
  machine-readable review state in PR comments

The structured recommendation remains visible in the review output, but GitHub
reviews are always submitted with the non-blocking `COMMENT` event. The
workflow never approves a pull request or formally requests changes. Failures
in the OpenAI SDK review step are advisory and do not fail the workflow.

During the SDK run, the context artifact and the GitHub Actions event payload
are temporarily removed from the filesystem and restored afterward. This keeps
model-visible review metadata on the dedicated `get_review_context` tool
boundary, prevents shell-tool access to the workflow event payload and bounded
PR body, and still retains the context artifact for auditability. The context
builder reads the PR body from the GitHub event file so multibyte
descriptions are not constrained by per-variable process environment limits.
Review shell commands run with only an allowlisted set of ordinary build and toolchain
environment variables and use an isolated home directory; GitHub, OpenAI, and
credential-bearing runner variables are not forwarded. The existing read,
build, and validation command surface remains unchanged.

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
`OPENAI_PROXY_TOKEN`; OpenAI SDK CLI login state is not reused. The review agent
explicitly uses high reasoning effort while leaving sampling temperature unset.
This prioritizes review accuracy over model latency and cost; the repair and
stabilization agents retain their existing model defaults.

Structure:

- `instructions.md`: static trusted review policy loaded directly into the SDK
  `Agent`; it contains no pull request values or runtime placeholders
- `../../../scripts/private/agent_runtime/`: shared helper modules for runtime
  setup, context generation, local review runs, artifact-state fetching, and
  publishing review output
- `../runtime/requirements-openai-agents.txt`: pinned OpenAI agent runtime dependencies
- `out/`: local and CI-generated review artifacts
