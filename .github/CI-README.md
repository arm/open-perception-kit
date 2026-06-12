# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Runs the actual checks

## What does `.github/workflows/codex-review.yml` do?

- Uses a prebuilt `codex-reviewer` container image from Artifactory to run a Codex review on PR open, reopen, synchronize, and ready-for-review events
- Requires `CODEX_REVIEWER_IMAGE_REPO`, `PEK_ARTIFACTORY_USERNAME`, `PEK_ARTIFACTORY_API_KEY`, `OPENAI_API_KEY`, and the workflow `GITHUB_TOKEN`
- Uses the checked-in `codex-reviewer` config under `.github/codex-reviewer/`
- Uses repository-specific review guidance from `.github/instructions/codex-review.instructions.md`
- Uploads `codex-reviewer-out` artifacts, including the generated prompt, raw JSON output, normalized review output, summary markdown, publish payload, and GitHub publish result
- Lets the `codex-reviewer` container publish the generated review summary and inline review comments to the PR from its generated JSON contract

## What does `.github/workflows/sync-rulesets.yml` do?

- Validates the checked-in JSON files under `.github/rulesets/`
- Applies repository rulesets from those JSON files on pushes to `main` and on manual dispatch
- Requires `RULESET_ADMIN_GITHUB_TOKEN` with repository administration write access

## Operational Notes

- Self-hosted runner workspace isolation and the `/work` ownership hazard are
  documented in [.github/ci/self-hosted-runner-workspace-isolation.md](ci/self-hosted-runner-workspace-isolation.md).

## Functionalities

- **Triggers:** Runs on pull requests, manual dispatch, and nightly schedule.
- **Branch and PR logic:** Only runs on non-draft PRs, or when the `run-pek-ci` label is added to a draft PR.
- **init-workspace:** Prepares the workspace and environment.
- **build-changed-applications:** Builds only the applications changed in a PR.
- **build-all-applications:** Builds all applications (nightly or manual trigger).
- **Codex review:** A separate workflow runs `codex-reviewer` from a published Artifactory image, uploads the generated artifacts for the PR, and publishes the summary plus inline review comments back to GitHub.
- **Ruleset sync:** A separate workflow applies the checked-in repository ruleset drafts to GitHub after they are merged to `main`.
