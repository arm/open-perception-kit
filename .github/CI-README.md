# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Runs the actual checks

## What does `.github/workflows/pr-bookkeeping.yml` do?

- Uses a prebuilt `pek-pr-automation` container image from Artifactory to regenerate the PR description body on PR open, reopen, synchronize, and ready-for-review events
- Requires `ARTIFACTORY_DOCKER_REGISTRY`, `PEK_ARTIFACTORY_USERNAME`, `PEK_ARTIFACTORY_API_KEY`, and `COPILOT_GITHUB_TOKEN`
- Runs the checked-in helpers under `scripts/ci/` so the same flow can be debugged locally
- Uses PR-specific Copilot guidance from `.github/instructions/pr.instructions.md`
- Lets Copilot inspect the PR diff through git instead of prebuilding a large context file

## What does `.github/workflows/publish-pr-automation-image.yml` do?

- Builds the `pek-pr-automation` Docker target
- Publishes the versioned image and `latest` tag to Artifactory for PR bookkeeping jobs

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
- **PR bookkeeping:** A separate workflow refreshes the Copilot-generated PR description body from the published Artifactory image.
- **Ruleset sync:** A separate workflow applies the checked-in repository ruleset drafts to GitHub after they are merged to `main`.
