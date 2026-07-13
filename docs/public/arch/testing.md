---
sidebar_position: 9
sidebar_label: Testing
---
# Testing, Validation and Verification

## Table of Contents

- [Valgrind](#valgrind-section)
    - [What the script checks](#valgrind-checks)
    - [How to add tests for new elements](#new-element-tests)
    - [Basic usage](#valgrind-basic-usage)
    - [Useful examples](#valgrind-examples)
    - [Debugging a failed testing pipeline](#valgrind-debugging)
    - [CI behavior](#valgrind-ci)

This document describes how our software is tested and how these tests can be run individually during local development.

<a id="valgrind-section"></a>
## Valgrind

Valgrind is used to check every PR for memory handling issues.

**Currently these Valgrind checks are not blocking any PRs, because there are many already existing issues which have to be addressed first.**

This section describes how to run memory checks with the Valgrind runner script.

- `scripts/testing/valgrind/test-elements-with-valgrind.sh`

The script builds Perception Experience Kit in debug mode, runs selected test pipelines through `pek-menu` under Valgrind, and writes one log file per pipeline to:

- `scripts/testing/valgrind/logs/`

<a id="valgrind-checks"></a>
### What the script checks

For each pipeline:

1. Runs `valgrind` with full leak checking.
2. Stores output in `scripts/testing/valgrind/logs/<pipeline-name>.valgrind.log.*` (PID-suffixed, including child processes).
3. Returns non-zero if one or more pipelines fail Valgrind checks.

By default, third-party suppressions are enabled with:

- `scripts/testing/valgrind/suppressed-warnings`

This keeps common framework noise out of the report so project issues are easier to spot.

<a id="new-element-tests"></a>
### How to add tests for new elements

When a new pipeline element is created, please add a simple pipeline to the scripts/pipelines/testing folder with as few and as simple elements as possible.
Pipelines created in this folder will be included in the valgrind check automatically.

<a id="valgrind-basic-usage"></a>
### Basic usage

From repo root:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh
```

This will:

- Build debug artifacts (`scripts/build-elements.sh debug`)
- Run valgrind with <u>**all**</u> JSON files in `scripts/pipelines/testing/`
    - Files prefixed with `DISABLED` are skipped

Clean build can be achieved with the clean command:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh clean
```

Show help:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --help
```

Verbose mode for debugging (adds `--verbose` to Valgrind):

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --verbose
```

<a id="valgrind-examples"></a>
### Useful examples

Run a single pipeline by stem (from `scripts/pipelines/testing/`):

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo
```

Run one pipeline by file name:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo.json
```

Run one pipeline by explicit path:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline ./scripts/pipelines/testing/only-onnx-yolo.json
```

Increase/limit processed frames (defaults to `30`):

```bash
NUM_FRAMES=120 ./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo
```

Disable suppressions to inspect all third-party warnings:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --show-3rd-party-warnings
```

Generate candidate suppression entries from Valgrind output:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --gen-suppressions --pipeline only-onnx-yolo
```

Run one pipeline with verbose Valgrind output (useful while investigating one failing case):

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo --verbose
```

Use a custom suppression file:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --suppressions-file ./scripts/testing/valgrind/suppressed-warnings --pipeline only-onnx-yolo
```

<a id="valgrind-debugging"></a>
### Debugging a failed testing pipeline

When a pipeline from `scripts/pipelines/testing/` fails (for example on the CI server during a PR check):

1. Re-run only the failing pipeline.

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo
```

2. Open the corresponding log:

```bash
less ./scripts/testing/valgrind/logs/only-onnx-yolo.valgrind.log.*
```

3. Jump to key summary lines:

```bash
rg -n "ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable" ./scripts/testing/valgrind/logs/only-onnx-yolo.valgrind.log.*
```

4. Analyse call stacks:

- Frames rooted in project sources (for example `development/elements/...` or `development/common/...`) usually indicate real Perception Experience Kit issues.
- Frames entirely in system/framework libraries are often third-party lifetime allocations and may belong in suppression tuning.

For deeper runtime diagnostics, re-run the failing pipeline with `--verbose`:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo --verbose
```

<a id="valgrind-ci"></a>
### CI behavior

In CI (`.github/workflows/pek-ci.yml`), the step named **Run Valgrind checks** runs this script via docker compose.

If that step fails, logs are uploaded as a workflow artifact named:

- `valgrind-logs`

This artifact contains files from `scripts/testing/valgrind/logs/` for offline analysis.
