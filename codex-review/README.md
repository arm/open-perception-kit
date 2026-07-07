# Codex Review

This directory owns the repository-specific Codex review flow.

The GitHub workflow renders the review prompt, runs `openai/codex-action`, then
publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings

Pull request runs include the PR title and URL in the rendered prompt and write
a bounded, deterministically extracted intent summary to
`codex-review/out/pr-intent.md`. The raw PR description is not copied into the
review prompt context. The reviewer treats extracted intent items as context for
intended behavior, while still reporting implementation bugs, unintended
regressions, contract mismatches, security issues, CI/release risk, and missing
validation for risky changes.

The workflow-scoped npm mirror config for `openai/codex-action` lives in
`codex-review/.npmrc`, so normal repo-root npm usage is not forced onto the
internal registry.

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Codex review runs
- `scripts/`: shared helper scripts for prompt rendering, local review runs, and
  publishing review output
- `out/`: local and CI-generated review artifacts
