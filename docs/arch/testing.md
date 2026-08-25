---
sidebar_position: 9
sidebar_label: Testing
---

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

`scripts/testing/valgrind/test-elements-with-valgrind.sh` builds debug artifacts,
runs selected testing pipelines under Valgrind, and writes logs under
`scripts/testing/valgrind/logs/`.

The runner supports all testing pipelines, a single pipeline by name or path,
clean builds, verbose output, third-party suppression control, and generated
suppression candidates. Run `--help` for the current option set.

Current CI runs Valgrind nightly, uploads raw logs as artifacts, and publishes
the collected baseline record count. Successful `main` and `develop` summaries
are stored as one current reference per branch at
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci/ci/valgrind-baselines/<branch>/valgrind-error-summary.xml`.
Pull-request jobs resolve the current target-branch head when validation starts
and accept the reference only when its recorded newest reference SHA matches
that head. If it is missing, invalid, or outdated, the trusted baseline
workflow schedules a backfill and the job waits for publication. Publishing a
new branch head replaces the previous reference; the GitHub Actions
`valgrind-baseline` artifact is only a one-day handoff between jobs.

These checks are not yet a hard quality gate because existing issues still
need to be addressed.

## Debugging Failures

For a failing pipeline, rerun only that pipeline and inspect the matching log:

```bash
./scripts/testing/valgrind/test-elements-with-valgrind.sh --pipeline only-onnx-yolo
less ./scripts/testing/valgrind/logs/only-onnx-yolo.valgrind.log.*
```

Search for Valgrind summary lines and prioritize call stacks rooted in project
sources such as `development/elements/` or `development/common/`. Frames entirely
inside framework or system libraries may belong in suppression tuning.

## Coverage Gaps

Known gaps are tracked in [Known Limitations](known-limitations.md). The main
areas needing stronger coverage are parser behavior against known tensors,
GStreamer element lifecycle behavior, and expected `Perception` output contracts.
