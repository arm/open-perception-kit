---
name: opk-release
description: Prepare, validate, and troubleshoot releases for this repository, including Semantic Version selection, changelog reconstruction, version updates, release pull requests, release-related Jira follow-ups, and release CI failures. Use for release branches, release PRs to main or develop, and investigations of release workflows; do not use for ordinary development or generic Semantic Versioning advice.
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->


# Release workflow

Use the repository and live services as the source of truth. Do not rely on remembered versions, checks, rulesets, sprint data, or commit IDs.

## Ground the release

1. Read `AGENTS.md`, `.github/CONTRIBUTING.md`, `docs/arch/release-process.md`, and `docs/public/branching-policy.md`.
2. Inspect `development/meson.build`, `CHANGELOG.md`, the release scripts, and the release workflows.
3. Inspect the current branch, worktree, remotes, and relevant commit history. Preserve unrelated user changes.
4. Query current GitHub and Jira state when the task depends on it. Do not infer live rulesets, PR checks, sprint fields, labels, or versions.

## Establish the baseline

1. Identify the latest genuine numbered product release before the proposed release. Cross-check the SemVer tag, GitHub Release, changelog entry, and version history; resolve conflicts before continuing.
2. Ignore documentation, asset, test, bootstrap, baseline, and other utility tags or pseudo-releases. Do not count or mention them as releases in `CHANGELOG.md`.
3. Compare the release source with the genuine predecessor. Use commits and merged PRs to reconstruct the user-visible changes, then group related changes without inventing claims.
4. Keep existing historical release entries intact unless the user explicitly asks to repair them.

## Select the Semantic Version

Classify the highest-impact releasable change against the public contract. The contract includes documented APIs, runtime behavior, configuration formats, metadata and wire formats, CLI behavior, and published package contents.

- Before `1.0.0`, use `PATCH` for backward-compatible fixes and `MINOR` for new functionality or breaking changes.
- Move to `1.0.0` only after an explicit API-stability decision.
- From `1.0.0`, use `MAJOR` for incompatible contract changes, `MINOR` for backward-compatible functionality, and `PATCH` for backward-compatible fixes.
- Recommend no release when there is no releasable product change.
- Propose only stable `MAJOR.MINOR.PATCH` versions while the checked-in publisher requires them.

Report the predecessor, proposed version, classification, and short rationale before editing version-bearing files. Treat a target version explicitly supplied by the user as confirmation after validating it against this policy; otherwise wait for confirmation.

## Prepare and validate

1. Update the authored active-version surfaces:
   - `development/meson.build`: authoritative product version.
   - `CHANGELOG.md`: one non-empty section for the same version.
   - `tools/plumber/pyproject.toml`: the exact `open_perception_kit==<version>` dependency.
   Do not rewrite historical examples or release records.
2. Regenerate both derived version surfaces, in this order:
   - `./scripts/perception-sdk.sh generate`
   - `./scripts/opksink-web.sh generate`
3. Verify the propagation with `./scripts/perception-sdk.sh check`, `./scripts/opksink-web.sh check`, and `./scripts/pre-commit/run.sh` before starting release CI.
4. Reuse `scripts/release/ReleaseTool.py` and the commands exercised by the current workflows. Do not duplicate release validation in the skill.
5. Rely on the release PR workflows for architecture packaging and smoke coverage that is unavailable locally.
6. Follow the current contribution rules for branch names, commits, PR titles, descriptions, and labels.

Failure pattern: changing only `development/meson.build` produces a stale Open Perception Kit SDK identity; regenerating the SDK without updating plumber makes the Docker dependency solve unsatisfiable. This sequence was verified by clean Open Perception Kit SDK and WebUI checks plus a successful local uv dependency solve. The ruled-out shortcut is a manual single-file version bump.

## Handle PRs, CI, and follow-ups

- Choose PR targets from the current branching policy and live rulesets. When changes must also remain on the development line, prepare the corresponding PR without assuming its target or merge order.
- Diagnose each failed or stuck check from its logs and event context before changing code. Fix the root cause; do not suppress findings, weaken thresholds, or expose self-hosted runners to fork code.
- Reuse an existing logical commit when the user asks to fold related fixes; otherwise add a focused commit. Preserve required commit-message trailers.
- For Jira follow-ups, summarize the problem and planned removal or change briefly. Derive sprint, labels, affected version, and fix version from current neighboring issues.
- Provide links to relevant PRs, reviews, jobs, releases, and Jira issues in the final report.

Do not commit, rewrite history, push, open PRs, create Jira issues, or merge unless the user has authorized that action. Never bypass branch protection or overwrite published release assets.
