# Codex Review

This directory owns the repository-specific Codex review flow.

The GitHub workflow fetches the latest prior Codex review state, renders the
review prompt, runs `openai/codex-action`, then publishes:

- one fresh summary comment per run
- reconciled inline review comments that are updated in place when the same
  finding is reported again

Per-finding Codex state lives on the inline comments themselves. Each inline
comment includes a `dismiss` checkbox that can be toggled in the GitHub UI; the
next run fetches that state and passes it back to Codex through the prior-state
JSON file.

Structure:

- `prompts/`: checked-in review prompt templates
- `schemas/`: structured output schemas for Codex review runs
- `scripts/`: shared helper scripts for prompt rendering, local review runs, and
  publishing and fetching review output/state
- `out/`: local and CI-generated review artifacts
