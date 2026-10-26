---
sidebar_position: 9
sidebar_label: Testing
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


# Testing, Validation, and Verification

This page summarizes the current validation surface for the runtime. The detailed
commands live in the scripts themselves; this page records what each path is
intended to prove.

## Element And Pipeline Checks

Testing pipelines live under `scripts/pipelines/testing/`. They should stay small
and isolate the element or behavior they are meant to validate. New elements
should add at least one simple pipeline there so the existing runners can execute
it automatically.

## Valgrind Checks

CI runs native Meson tests under Valgrind on relevant pull requests and pushes.
It compares normalized results against `.github/ci/baselines/valgrind-baseline.xml`
and fails when new repository-owned errors appear. Raw logs are uploaded as the
`valgrind-test-logs` artifact.

## Local Test Coverage

In the development container, configure a debug build with tests and Meson
coverage instrumentation enabled. Run the tests to collect coverage data, then
use Meson's built-in target to generate the HTML report:

```bash
./scripts/build.sh debug true --extra-setup-args=-Db_coverage=true,--wipe
meson test -C development/build-active --print-errorlogs
ninja -C development/build-active coverage-html
```

The report is written to
`development/build-active/meson-logs/coveragereport/index.html`.

## Local Clang-Tidy Check

In the development container, build the desired configuration and run the
`clang-tidy-all` target:

```bash
./scripts/build.sh debug true
ninja -C development/build-active clang-tidy-all
```

The target uses the repository `.clang-tidy` rules and the active build's
compilation database. It prints findings in the terminal and writes
`development/build-active/meson-logs/clang-tidy-all.md`, with the full tool output
in the sibling `clang-tidy-all.log`. Each run replaces these reports.

Analysis covers authored C/C++ sources enabled in that build and their included
project headers. Dependencies, generated SDK sources, and disabled components
are excluded. Repeated findings from shared headers are shown once. Warnings
are advisory; tool or compilation failures fail the target and mark the report
incomplete. Source files are never automatically fixed.

The target runs up to four clang-tidy processes concurrently. Set
`CLANG_TIDY_JOBS=2` before the command to reduce memory use, or set `CLANG_TIDY`
to select a different executable. Tests need not be enabled to use this target;
enabling them also includes their compiled sources in the analysis.

To show findings only on new code, choose the target branch and run:

```bash
CLANG_TIDY_BASE=origin/develop ninja -C development/build-active clang-tidy-new
```

This writes `meson-logs/clang-tidy-new.md` and `clang-tidy-new.log` under the
active build directory, independently of the overall report. The terminal and
Markdown show only findings whose primary location is on an added or modified
C/C++ source or header line since the merge base with the selected reference.
Tracked staged and unstaged edits are included. Stage new files (or use
`git add -N`) and reconfigure the build to include new translation units.
Untracked files and deletion-only changes are excluded; a rename alone does
not make the file's existing lines new.

Without `CLANG_TIDY_BASE`, the comparison uses `PULL_REQUEST_TARGET_BRANCH`,
then `branch.<current-branch>.vscode-merge-base`, then the local `origin/HEAD`.
The resolved reference and merge-base commit appear in the terminal and report.
Refs are not fetched automatically; an unavailable reference fails the target
and marks its report incomplete.

For source-only changes, the new-code target runs clang-tidy only on changed
source files present in the active compilation database. If a project header
was changed, added, deleted, or renamed, it analyzes the full configured source
set to cover its includers. The selected mode and number of translation units
appear in the terminal and report. If no C/C++ lines were added or modified,
analysis is skipped.

Both modes filter findings by changed lines. Findings triggered by a change but
located on unchanged lines are not included; use `clang-tidy-all` to see those.
Tool and compilation failures in analyzed files remain visible regardless of
their location. The source-only mode does not check unchanged translation units.

## Coverage Gaps

Known gaps are tracked in [Known Limitations](known-limitations.md). The main
areas needing stronger coverage are parser behavior against known tensors,
GStreamer element lifecycle behavior, and expected `FrameResults` output contracts.
