# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Runs the actual checks

## What does `.github/workflows/codex-review.yml` do?

- Uses `openai/codex-action` directly to run a Codex review on PR open, reopen, synchronize, and ready-for-review events
- Supports `workflow_dispatch` manual runs with a configurable `base_ref` input for the diff baseline
- Requires `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` and the workflow `GITHUB_TOKEN`
- Uses the checked-in review assets under `codex-review/`
- Keeps prompt templates in `codex-review/prompts/`
- Keeps schemas in `codex-review/schemas/`
- Keeps shared scripts in `codex-review/scripts/`
- Includes PR title and URL in the rendered prompt, and writes bounded extracted
  intent context to `codex-review/out/pr-intent.md` so intended behavior changes
  can be considered without copying the raw PR body into review context
- Intent shall be summarized in short bullet points under the headings of the predefined headers of the PR description template to be safely captured. The description is filtered for malicious context trying to misguide the reviewer agent both by telling the agent not to trust the content of this prompt and by the render_promt shell script as well.
- Uploads `codex-review-out` artifacts, including the rendered prompt, extracted
  intent context, raw JSON output, and summary markdown
- Publishes a fresh PR summary comment for each run from the structured review output
- Publishes fresh inline review comments for the current findings without prior-state reconciliation

## What does `.github/workflows/workflow-audit.yml` do?

- Runs a minimal dependency freshness report for external GitHub Actions used by repository workflows
- Compares the current `uses:` refs against the latest GitHub release/tag for each action repository
- Publishes one simple Markdown report and a lightweight JSON snapshot in the `workflow-dependency-freshness` artifact

## Operational Notes

- Self-hosted runner workspace isolation and the `/work` ownership hazard are
  documented in [.github/ci/self-hosted-runner-workspace-isolation.md](ci/self-hosted-runner-workspace-isolation.md).

## Functionalities

- **Triggers:** Runs on pull requests, manual dispatch, and nightly schedule.
- **Branch and PR logic:** Only runs on non-draft PRs, or when the `run-pek-ci` label is added to a draft PR.
- **init-workspace:** Prepares the workspace and environment.
- **build-changed-applications:** Builds only the applications changed in a PR.
- **build-all-applications:** Builds all applications (nightly or manual trigger).
- **Codex review:** A separate workflow runs codex review, uploads the generated artifacts for the PR, posts a fresh summary comment for each run, and publishes inline review comments for the current findings.
- **Ruleset sync:** A separate workflow applies the checked-in repository ruleset drafts to GitHub after they are merged to `main`.
