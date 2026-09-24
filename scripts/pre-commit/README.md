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

# pre-commit scripts

This folder owns the host-side pre-commit container flow for `open-perception-kit`.

## Scope

This folder intentionally keeps the rollout narrow:

- host shell setup and hook installation
- dedicated Docker runtime for the local quality checks
- portable git hook wrappers
- host-side smoke coverage

Out of scope here:

- Dev Container hook adoption
- CI workflow ownership

## Entry Points

Run the one-time host setup:

```bash
./scripts/pre-commit/setup.sh
```

After that, normal host-side commits use the installed hooks:

```bash
git commit
```

Manual entrypoints:

- `./scripts/pre-commit/run.sh`
- `./scripts/pre-commit/run.sh full`
- `./scripts/pre-commit/testing/host-smoke.sh`

## Behavior

The host-only wrapper keeps the existing local hook intent through shared
`opk-ci` presets:

- `--pre-commit-fix`: `clang-format`, `python-format`, `cmake-format`,
  `shell-format`, `license-header`, and `actionlint`.
- `--pre-commit-check`: the check-only equivalent used by CI and manual
  verification.
- `--ci-pr-checks`: PR quality gate, adding branch naming, CI commit-message,
  Agent runtime static analysis, and config descriptor validation to `--pre-commit-check`.
- `--ci-full-checks`: full/nightly quality gate, adding Agent runtime static
  analysis and config descriptor validation to `--pre-commit-check`.

Light mapping:

- Dev Container pre-commit hook: runs `opk-ci --pre-commit-fix` plus the commit metadata hooks.
- Host `./scripts/pre-commit/run.sh`: runs the same `--pre-commit-fix` bundle on the host through the dedicated container.
- Host `./scripts/pre-commit/run.sh commit-msg <path>`: mirrors the `commit-msg` hook path.
- CI PR quality: runs `opk-ci --ci-pr-checks`.
- CI full quality: runs `opk-ci --ci-full-checks`.

Scope still differs by entry point: local/container pre-commit receives the
file list from pre-commit, host pre-commit uses staged files with a branch-delta
fallback against `PULL_REQUEST_TARGET_BRANCH`, branch merge-base config, or the
remote default branch, CI PR uses `--pr-target-branch`, and CI full/nightly
checks the tracked tree.

The wrapper builds the dedicated runtime image during setup and refreshes it
before hook execution. Docker's build cache keeps unchanged runs cheap while
ensuring pulled updates to `tools/opk-ci`, runtime packages, or the
Dockerfile are picked up locally.

If you keep multiple local clones with the same checkout directory name, set
`REPO_CHECKS_IMAGE_NAME=<unique-tag>` for both `setup.sh` and `run.sh` to
avoid local Docker image tag collisions between checkouts.

To keep local worktree checkouts working, the runtime mounts both the working
tree and the external git common directory when `.git` points outside the
checkout.
