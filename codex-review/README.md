# Codex Review

This directory owns the repository-specific Codex review flow.

The GitHub workflow renders the review prompt, runs `openai/codex-action`, then
publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings

The workflow-scoped npm mirror config for `openai/codex-action` lives in
`codex-review/.npmrc`, so normal repo-root npm usage is not forced onto the
internal registry.

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Codex review runs
- `scripts/`: shared helper scripts for prompt rendering, local review runs, and
  publishing review output
- `out/`: local and CI-generated review artifacts
