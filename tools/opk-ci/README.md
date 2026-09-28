# opk-ci

## Overview
Purpose of this project is to unify quality checks across different local hosts and CI for the Open Perception Kit project
It ensures consistent code quality, formatting, and license compliance for all contributors, regardless of host platform.

## Features

- PEP-8 Python formatting and checks
- CMake formatting and checks
- Shell script formatting and checks
- License header checks and insertion
- Secret scanning with `detect-secrets` and `.secrets.baseline`
- Branch naming checks 
- Commit message checks
- GitHub Actions workflow linting with `actionlint`
- Clang-format checks 
- clang-tidy checks (advisory)
- Default startup and final summary report with effective checks and file scope
- Optional plain-text report artifact via `--report-file`
- Run on all files, changed files, or a custom file list
- Verbose logging and configurable output (stdout, file, both)
- Integration with pre-commit hooks and CI pipelines is available in the ![Open Perception Kit repository](https://github.com/arm/open-perception-kit/)

## Usage

After installation, you can run the tool via:

```bash
opk-ci --help
```

Example usage:

```bash
opk-ci --pre-commit-fix --list-of-files src/main.cpp scripts/check.sh
opk-ci --pre-commit-check --pr-target-branch main
opk-ci --ci-pr-checks --pr-target-branch main
opk-ci --ci-full-checks
opk-ci --python-format-check --cmake-format-check
opk-ci --actionlint
opk-ci --check-secrets --list-of-files .github/workflows/opk-ci.yml
opk-ci --ci-pr-checks --pr-target-branch main --report-file artifacts/opk-ci-report.txt
opk-ci --license-header --list-of-files src/main.cpp src/util.py
```

## Presets

Use presets for repository entry points so local, host, and CI flows do not
copy the same check list in multiple places.

| Flag | Checks | Intended use |
| --- | --- | --- |
| `--pre-commit-fix` | `--clang-format`, `--python-format`, `--cmake-format`, `--shell-format`, `--license-header`, `--check-secrets`, `--actionlint` | Local/container and host pre-commit paths that may update files in place. |
| `--pre-commit-check` | `--clang-format-check`, `--python-format-check`, `--cmake-format-check`, `--shell-format-check`, `--license-header-check`, `--check-secrets`, `--actionlint` | Check-only equivalent of the pre-commit bundle, useful for manual verification and CI. |
| `--ci-pr-checks` | `--pre-commit-check`, `--branch-naming`, `--commit-msg-ci`, `--agent-runtime-static-analysis`, `--config-schema-check` | Pull request quality gate. The descriptor check delegates to the shared C++ parse, schema, and semantic validator. Pair with `--pr-target-branch <branch>` for PR delta scope. |
| `--ci-full-checks` | `--pre-commit-check`, `--agent-runtime-static-analysis`, `--config-schema-check` | Full/nightly quality gate. The descriptor check delegates to the shared C++ parse, schema, and semantic validator. Without an explicit file or PR scope, this checks the tracked tree. |

`--all-checks` is kept for compatibility. New workflow wiring should prefer
the explicit CI presets above.

### clang-tidy

Build the project first so Meson generates
`development/build/compile_commands.json`, then run clang-tidy on all compiled
files or on an explicit file list:

```bash
./scripts/build.sh debug true
rm -f clang-tidy.log
opk-ci --clang-tidy --log-output both --log-file clang-tidy.log
rm -f clang-tidy.log
opk-ci --clang-tidy \
  --list-of-files development/elements/opktracker/Tracker.cpp \
  --log-output both \
  --log-file clang-tidy.log
opk-ci --clang-tidy-stats clang-tidy.log
```

By default, `opk-ci` uses `development/build/compile_commands.json`. Use
`--compile-commands-dir` only when checking against a different build
directory. If `clang-tidy` is not on `PATH`, `opk-ci` also checks the active
Python environment; CI can pass `--clang-tidy-binary` explicitly if needed.
Files not listed directly in the active compile database are skipped.

The repository `.clang-tidy` policy starts with a small SonarQube-aligned
advisory set. Some SonarQube rules have no exact clang-tidy equivalent, and
some clang-tidy findings are extra local guidance rather than SonarQube parity.

`--clang-tidy-stats` parses a saved clang-tidy log and reports how many
diagnostics each clang-tidy check emitted. This is useful after increasing the
enabled ruleset and running clang-tidy with
`--log-output both --log-file clang-tidy.log`. Use
`--clang-tidy-stats-output` to also write the statistics as JSON.

CI can compare those statistics with a repository baseline:

```bash
opk-ci --clang-tidy-stats clang-tidy.log \
  --clang-tidy-stats-output clang-tidy-stats.json \
  --clang-tidy-baseline .github/ci/baselines/clang-tidy-baseline.json \
  --clang-tidy-baseline-mode enforce
```

Baseline comparison checks per-rule counts only. It does not enforce the total
diagnostic count, so unrelated cleanup cannot hide a regression in another
clang-tidy check. When a rule regresses, the failure summary repeats the
matching diagnostic locations, explanations, and source excerpts from the log.
For a rule that already has accepted findings, the summary shows every current
location and explains that a count-only baseline cannot identify which specific
locations are new.

To lower the repository baseline after fixes, run:

```bash
opk-ci --clang-tidy-stats clang-tidy.log \
  --clang-tidy-baseline .github/ci/baselines/clang-tidy-baseline.json \
  --clang-tidy-update-baseline
```

The update refuses to write the baseline if any current per-rule count is higher
than the existing accepted count. When file logging is enabled, `opk-ci`
refuses to reuse an existing log file so statistics are not polluted by appended
output from older runs.

## Installation

`opk_ci` is installed automatically during workspace setup (see `setup_workspace.sh`). For manual installation:

1. **Create and activate a Python virtual environment (recommended):**
   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   ```
2. **Install opk-ci in editable mode:**
   ```bash
   pip install -e tools/opk-ci
   ```
3. **(Optional) Install pre-commit hooks:**
   ```bash
   pre-commit install
   pre-commit install --hook-type commit-msg
   ```

The setup script also registers a shell function in your `.bashrc` for easy usage and argomplete:

```bash
opk-ci() {
    source $WORKSPACE_DIR/.venv/bin/activate
    $WORKSPACE_DIR/.venv/bin/python -m opk_ci "$@"
    deactivate
}
eval "$($WORKSPACE_DIR/.venv/bin/register-python-argcomplete opk-ci)"
```

## Integration in 

- **CI:** opk_ci runs automatically in CI pipelines (see `.github/workflows/opk-ci.yml`).
- **Local:** You can run opk_ci manually, via pre-commit, or as VS Code task.

## License

Copyright 2025-2026 Arm Limited and/or its affiliates
