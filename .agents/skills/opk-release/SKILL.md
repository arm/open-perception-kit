---
name: opk-release
description: Prepare, validate, and troubleshoot releases for this repository, including Semantic Version selection, changelog reconstruction, version updates, release pull requests, release-related Jira follow-ups, and release CI failures. Use for release branches, release PRs to main or develop, and investigations of release workflows; do not use for ordinary development or generic Semantic Versioning advice.
---

# Release workflow

Use the repository and live services as the source of truth. Do not rely on remembered versions, checks, rulesets, sprint data, or commit IDs.

## Ground the release

1. Read `AGENTS.md`, `.github/CONTRIBUTING.md`, `docs/arch/release-process.md`, and `docs/public/branching-policy.md`.
2. Inspect `development/meson.build`, `CHANGELOG.md`, the release scripts, and the release workflows.
3. Inspect the current branch, worktree, remotes, and relevant commit history. Preserve unrelated user changes.
4. Query current GitHub and Jira state when the task depends on it. Do not infer live rulesets, PR checks, sprint fields, labels, or versions.

## Choose the branch lineage

- For a normal release, create `release/*` from the current `origin/develop` and target `main`.
- Use exactly one `release/*` branch for a release. Merge it into `main`, then back-merge the resulting `main` head into `develop`; never substitute another release branch or content-only cherry-picks for the back-merge.
- For a post-release hotfix, create `hotfix/*` from the current `origin/main`, target `main`, and return the fix to `develop` after merge.
- Never rebuild a normal release as `main` plus selected `develop` commits, even to reduce the pull-request diff or avoid a conflict.
- If `main` and `develop` exceptionally diverged after a back-merge, keep `develop` as the release source and first parent while reconciling `main` on the release branch. Resolve every conflict in favor of `develop`; it is the release ground truth.
- Before the first push, verify that `origin/develop` is an ancestor of a normal release branch. After opening the pull request, verify its base is `main`.

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

1. Update the authoritative version in `development/meson.build` and only the active version surfaces discovered from the repository. Do not rewrite historical examples or release records.
2. Add a non-empty `CHANGELOG.md` section in the existing format. This is mandatory: before the first push, verify that the direct diff against `origin/main` contains both the exact release version and its changelog entry. Describe product changes, not release mechanics or ignored utility releases.
3. Reuse `scripts/release/ReleaseTool.py` and the commands exercised by the current workflows. Do not duplicate release validation in the skill.
4. Run the smallest relevant local checks, then rely on the release PR workflows for architecture packaging and smoke coverage that is unavailable locally.
5. Follow the current contribution rules for branch names, commits, PR titles, descriptions, and labels.

## Handle PRs, CI, and follow-ups

- Choose PR targets from the current branching policy and live rulesets. When changes must also remain on the development line, prepare the corresponding PR without assuming its target or merge order.
- Diagnose each failed or stuck check from its logs and event context before changing code. Fix the root cause; do not suppress findings, weaken thresholds, or expose self-hosted runners to fork code.
- Reuse an existing logical commit when the user asks to fold related fixes; otherwise add a focused commit. Preserve required commit-message trailers.
- For Jira follow-ups, summarize the problem and planned removal or change briefly. Derive sprint, labels, affected version, and fix version from current neighboring issues.
- Provide links to relevant PRs, reviews, jobs, releases, and Jira issues in the final report.

Do not commit, rewrite history, push, open PRs, create Jira issues, or merge unless the user has authorized that action. Never bypass branch protection or overwrite published release assets.
