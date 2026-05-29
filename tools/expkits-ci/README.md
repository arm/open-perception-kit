# expkits_ci

## Overview
Purpose of this project is to unify quality checks across different local hosts and CI for the Edge AI Experience Kits project
It ensures consistent code quality, formatting, and license compliance for all contributors, regardless of host platform.

## Features

- PEP-8 Python formatting and checks
- CMake formatting and checks
- Shell script formatting and checks
- License header checks and insertion
- Secret scanning with `detect-secrets` and `.secrets.baseline`
- Branch naming checks 
- Commit message checks
- Clang-format checks 
- clang-tidy checks (advisory)
- Default startup and final summary report with effective checks and file scope
- Optional plain-text report artifact via `--report-file`
- Run on all files, changed files, or a custom file list
- Verbose logging and configurable output (stdout, file, both)
- Integration with pre-commit hooks and CI pipelines is available in the ![Edge AI Experience Kits repository](https://github.com/Arm-Debug/edge-ai-zephyr-experience-kits/)

## Usage

After installation, you can run the tool via:

```bash
expkits-ci --help
```

Example usage:

```bash
expkits-ci --all-checks
expkits-ci --python-format-check --cmake-format-check
expkits-ci --check-secrets --list-of-files .github/workflows/pek-ci.yml
expkits-ci --all-checks --pr-target-branch main --report-file artifacts/expkits-ci-report.txt
expkits-ci --license-header --list-of-files src/main.cpp src/util.py
```

### clang-tidy

Build the project first so Meson generates
`development/build/compile_commands.json`, then run clang-tidy on all compiled
files or on an explicit file list:

```bash
./scripts/build-elements.sh debug true
expkits-ci --clang-tidy
expkits-ci --clang-tidy --list-of-files development/elements/pektracker/Tracker.cpp
expkits-ci --clang-tidy-stats clang-tidy.log
```

By default, `expkits-ci` uses `development/build/compile_commands.json`. Use
`--compile-commands-dir` only when checking against a different build
directory. If `clang-tidy` is not on `PATH`, `expkits-ci` also checks the active
Python environment; CI can pass `--clang-tidy-binary` explicitly if needed.
Files not listed directly in the active compile database are skipped.

The repository `.clang-tidy` policy starts with a small SonarQube-aligned
advisory set. Some SonarQube rules have no exact clang-tidy equivalent, and
some clang-tidy findings are extra local guidance rather than SonarQube parity.

`--clang-tidy-stats` parses a saved clang-tidy log and reports how many
diagnostics each clang-tidy check emitted. This is useful after increasing the
enabled ruleset and running with `--log-output both --log-file clang-tidy.log`.
When file logging is enabled, `expkits-ci` refuses to reuse an existing log file
so statistics are not polluted by appended output from older runs.

## Installation

`expkits_ci` is installed automatically during workspace setup (see `setup_workspace.sh`). For manual installation:

1. **Create and activate a Python virtual environment (recommended):**
   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   ```
2. **Install expkits-ci in editable mode:**
   ```bash
   pip install -e common/tools/scripts/expkits-ci
   ```
3. **(Optional) Install pre-commit hooks:**
   ```bash
   pre-commit install
   pre-commit install --hook-type commit-msg
   ```

The setup script also registers a shell function in your `.bashrc` for easy usage and argomplete:

```bash
expkits-ci() {
    source $WORKSPACE_DIR/.venv/bin/activate
    $WORKSPACE_DIR/.venv/bin/python -m expkits-ci "$@"
    deactivate
}
eval "$($WORKSPACE_DIR/.venv/bin/register-python-argcomplete expkits-ci)"
```

## Integration in 

- **CI:** expkits_ci runs automatically in CI pipelines (see `.github/workflows/expkits-ci.yml`).
- **Local:** You can run expkits_ci manually, via pre-commit, or as VS Code task.

## License

Copyright (C) 2025 Arm Limited. All rights reserved.
