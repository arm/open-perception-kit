# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Runs the actual checks

## What does `.github/workflows/codex-review.yml` do?

- Runs Codex review on a self-hosted runner through the shared Python OpenAI Agents SDK runner
- Supports `workflow_dispatch` manual runs with a configurable `base_ref` input for the diff baseline
- Uses `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS`, the Arm OpenAI proxy endpoint, and tracing-disabled Agents SDK execution
- Uses the checked-in review assets under `codex-review/`
- Keeps prompt templates in `codex-review/prompts/`
- Keeps schemas in `codex-review/schemas/`
- Keeps shared scripts in `codex-review/scripts/`
- Uploads `codex-review-out` artifacts, including the rendered prompt, raw JSON output, and summary markdown
- Publishes a fresh PR summary comment for each run from the structured review output
- Publishes fresh inline review comments for the current findings without prior-state reconciliation

## What does `.github/workflows/workflow-audit.yml` do?

- Runs a minimal dependency freshness report for external GitHub Actions used by repository workflows
- Compares the current `uses:` refs against the latest GitHub release/tag for each action repository
- Publishes one simple Markdown report and a lightweight JSON snapshot in the `workflow-dependency-freshness` artifact
- On nightly schedule and manual dispatch, stays a single nightly pipeline by handing any auto-fixable freshness delta directly into the reusable repair core instead of spawning a second standalone workflow
- On pull requests, only reruns the report job so repair PR validation can confirm the same evidence without recursively opening more repair PRs
- Keeps the originating report artifact as the repair starting point, so the generated PR still carries the original evidence trail, badge, and bot metadata

## What does `.github/workflows/workflow-action-update-agent.yml` do?

- Acts as the manual fallback caller for the reusable repair core
- Accepts a source run ID manually and forwards execution into the reusable workflow implementation with inherited secrets plus a selected repair profile

## What does `.github/workflows/workflow-action-update-agent-reusable.yml` do?

- Contains the core repair engine behind the caller workflow
- Resolves source-run metadata, downloads logs and artifacts, and creates `goal.md` plus the companion Markdown context files under `.codex/workflow-action-update-agent/`
- Feeds the collected failure state and any downloaded artifact context into the OpenAI SDK repair agent so the patch is generated from the report instead of from inline workflow logic
- Runs the same shared Python OpenAI Agents SDK path as `codex-review`, with the Arm proxy and tracing disabled, to generate the repair patch
- Opens a draft repair PR, applies the profile-defined rerun label, then keeps a single stabilization loop: wait for the standard Codex review, feed non-approve findings back into the SDK agent on the same branch, rerun validation, and merge only after the latest PR head is fully green
- Stays orchestration-thin by delegating repo-specific helper commands to a local composite action and flow policy to the repair profile
- Uses `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` for the OpenAI SDK step so the repair flow matches `codex-review`
- Supports the optional `EXPKITS_AGENT_TOKEN` secret so checkout, push, PR, and merge operations can run under a PAT or GitHub App token instead of the default `GITHUB_TOKEN`

## What does `scripts/private/openai_agent_runner.py` do?

- Provides the shared Python OpenAI Agents SDK entrypoint for review, repair, and stabilization jobs
- Sets the Arm OpenAI proxy base URL, maps `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` into `OPENAI_API_KEY`, disables Agents SDK tracing, and injects `truststore` before importing OpenAI libraries
- Runs from the workflow-local `.codex/openai-agent-venv` environment so Ubuntu's externally managed system Python is left untouched
- Writes structured Codex review JSON for `codex-review` and lets repair/stabilization agents inspect the repo, run validation commands, and apply minimal patches without owning branch or PR lifecycle operations

## What does `scripts/private/workflow_action_update_agent.py` do?

- Holds the small repo-specific building blocks that would otherwise bloat the workflow YAML
- Loads the selected repair profile, resolves source-run inputs, collects workflow evidence, builds runtime Markdown inputs, and renders repair PR metadata
- Packages repository changes, pushes repair branches, opens draft PRs, and owns the stabilization loop that waits on review state, generates follow-up fixes, reruns local validation, and merges successful repairs
- Reuses `.github/PULL_REQUEST_TEMPLATE.md` through explicit marker sections instead of brittle free-text replacement, and injects the repair CI badge only for bot-authored PRs
- Provides the reusable PR-lifecycle lego layer that future caller workflows can build on without dragging in the current experiment branches

## What does `.github/ci/workflow-action-update-agent/` do?

- Stores the static Markdown prompt templates plus the repair profile JSON files used by the repair agent core
- The default profile lives at `.github/ci/workflow-action-update-agent/profile.json`
- The nightly workflow-freshness profile lives at `.github/ci/workflow-action-update-agent/workflow-audit-profile.json`
- Keeps long review and constraint text out of the workflow YAML and Python helper while letting the profile carry flow-specific policy such as validation workflows, labels, prompt context files, and the Codex model selection
- Lets the helper still generate the final `.codex/workflow-action-update-agent/*.md` files on the fly at runtime, so callers reuse the same core without checking generated prompt files into git

## What does `.github/actions/workflow-action-update-agent-helper/` do?

- Wraps the Python helper behind one local composite action so the reusable workflow stays declarative and avoids repeating long inline `python3 ...` run blocks
- Exposes stable outputs such as `should_run`, `repair_branch`, `codex_model`, `has_changes`, `head_sha`, and `pr_number`
- Gives future caller workflows the same helper API without copying shell glue

## Operational Notes

- Self-hosted runner workspace isolation and the `/work` ownership hazard are
  documented in [.github/ci/self-hosted-runner-workspace-isolation.md](ci/self-hosted-runner-workspace-isolation.md).
