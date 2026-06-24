# Codex Review

This directory owns the repository-specific Codex review flow.

The GitHub workflow renders the review prompt, runs `openai/codex-action`, then
publishes:

- one fresh summary comment per run
- fresh inline review comments for the current findings

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Codex review runs
- `scripts/`: shared helper scripts for prompt rendering, local review runs, and
  publishing review output
- `out/`: local and CI-generated review artifacts
