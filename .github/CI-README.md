# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Runs the actual checks

## What does `.github/workflows/agent-review.yml` do?

- Runs Agent review on a self-hosted runner through the shared Python OpenAI Agents SDK runner
- Supports `workflow_dispatch` manual runs with a configurable `base_ref` input for the diff baseline
- Uses `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS`, the Arm OpenAI proxy endpoint, and tracing-disabled Agents SDK execution
- Uses the checked-in review assets under `.github/agent-runtime/review/`
- Keeps prompt templates in `.github/agent-runtime/review/prompts/`
- Keeps schemas in `.github/agent-runtime/review/schemas/`
- Reuses shared helper modules from `scripts/private/agent_runtime/`
- Uploads `agent-review-out` artifacts, including the rendered prompt, raw JSON output, and summary markdown
- Treats `agent-review-out/review.json` as the canonical machine-readable review state
- Publishes a fresh PR summary comment for each run from the structured review output
- Publishes fresh inline review comments for the current findings without prior-state reconciliation

## What does `.github/agent-runtime/` do?

- Stores Agent runtime assets only: prompts, schemas, profiles, dependency pins, and model/task config
- Does not define executable GitHub Actions workflows; those live only in `.github/workflows/`

## What does `.github/workflows/workflow-audit.yml` do?

- Runs a minimal dependency freshness report for external GitHub Actions used by repository workflows
- Compares the current `uses:` refs against the latest GitHub release/tag for each action repository
- Publishes one simple Markdown report and a lightweight JSON snapshot in the `workflow-dependency-freshness` artifact
- On pull requests, reruns only the report job so workflow changes can validate the same dependency evidence without opening repair PRs

## What does `.github/workflows/workflow-action-update-agent.yml` do?

- Acts as the manual caller for the reusable repair core
- Accepts a source run ID manually and forwards execution into the reusable workflow implementation with inherited secrets plus a selected repair profile

## What does `.github/workflows/workflow-action-update-agent-reusable.yml` do?

- Contains the core repair engine behind the caller workflow
- Resolves source-run metadata, downloads logs and artifacts, and creates `goal.md` plus the companion Markdown context files under `.agent-runtime/workflow-action-update-agent/`
- Feeds the collected failure state and any downloaded artifact context into the OpenAI SDK repair agent so the patch is generated from the report instead of from inline workflow logic
- Runs the same shared Python OpenAI Agents SDK path as `agent-review`, with the Arm proxy and tracing disabled, to generate the repair patch
- Opens a draft repair PR only when the source run belongs to a PR carrying the profile-defined repair authorization label, applies the profile-defined rerun label, then keeps a single stabilization loop: wait for the standard Agent review, feed non-approve findings back into the SDK agent on the same branch, rerun validation, and merge only after the latest PR head is fully green
- Stays orchestration-thin by delegating repo-specific helper commands to a local composite action and flow policy to the repair profile
- Uses `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` for the OpenAI SDK step so the repair flow matches `agent-review`
- Supports the optional `EXPKITS_AGENT_TOKEN` secret so checkout, push, PR, and merge operations can run under a PAT or GitHub App token instead of the default `GITHUB_TOKEN`

## What does `scripts/private/agent_runtime/openai_agent_runner.py` do?

- Provides the shared Python OpenAI Agents SDK entrypoint for review, repair, and stabilization jobs
- Sets the Arm OpenAI proxy base URL, maps `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` into `OPENAI_API_KEY`, disables Agents SDK tracing, and injects `truststore` before importing OpenAI libraries
- Resolves the model from `.github/agent-runtime/runtime/agent-models.json` by agent instance, while still accepting an explicit `--model` override from trusted workflow plumbing
- Resolves task ownership and limits from `.github/agent-runtime/runtime/agent-tasks.json`, then dispatches through checked-in task classes instead of embedding task-specific behavior in the generic entrypoint
- Runs from the workflow-local `.agent-runtime/openai-agent-venv` environment so Ubuntu's externally managed system Python is left untouched
- Writes structured Agent review JSON for `agent-review` and lets repair/stabilization agents inspect the repo, run validation commands, and apply minimal patches without owning branch or PR lifecycle operations

## What does `scripts/private/workflow_action_update_agent/` do?

- Holds the small repo-specific building blocks that would otherwise bloat the workflow YAML
- Loads the selected repair profile, resolves source-run inputs, collects workflow evidence, builds runtime Markdown inputs, and renders repair PR metadata
- Packages repository changes, pushes repair branches, opens draft PRs, and owns the stabilization loop that waits on review state, generates follow-up fixes, reruns local validation, and merges successful repairs
- Reuses `.github/PULL_REQUEST_TEMPLATE.md` through explicit marker sections instead of brittle free-text replacement, and injects the repair CI badge only for bot-authored PRs
- Provides the PR-lifecycle layer used by the checked-in repair and stabilization workflows

## What does `.github/agent-runtime/workflow-action-update-agent/` do?

- Stores the static Markdown prompt templates plus the repair profile JSON files used by the repair agent core
- The default profile lives at `.github/agent-runtime/workflow-action-update-agent/profiles/profile.json`
- The nightly workflow-freshness profile lives at `.github/agent-runtime/workflow-action-update-agent/profiles/workflow-audit-profile.json`
- Runtime task limits and default task-to-agent-instance mapping live in `.github/agent-runtime/runtime/agent-tasks.json`
- Keeps long review and constraint text out of the workflow YAML and Python helper while letting the profile carry flow-specific policy such as validation workflows, labels, prompt context files, and the model config path
- Lets the helper still generate the final `.agent-runtime/workflow-action-update-agent/*.md` files on the fly at runtime, so callers reuse the same core without checking generated prompt files into git

## What does `.github/actions/workflow-action-update-agent-helper/` do?

- Wraps the Python helper behind one local composite action so the reusable workflow stays declarative and avoids repeating long inline `python3 ...` run blocks
- Exposes stable outputs such as `should_run`, `repair_branch`, `agent_model`, `has_changes`, `head_sha`, and `pr_number`
- Keeps the workflow YAML from copying long helper command blocks

## Operational Notes

- Self-hosted runner workspace isolation and the `/work` ownership hazard are
  documented in [.github/ci/self-hosted-runner-workspace-isolation.md](ci/self-hosted-runner-workspace-isolation.md).
