# expkits_ci

## Overview
Purpose of this project is to unify quality checks across different local hosts and CI for the Edge AI Experience Kits project
It ensures consistent code quality, formatting, and license compliance for all contributors, regardless of host platform.

## Features

- PEP-8 Python formatting and checks
- CMake formatting and checks
- Shell script formatting and checks
- License header checks and insertion
- Branch naming checks 
- Commit message checks
- Clang-format checks 
- clang-tidy checks (advisory)
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
expkits-ci --all-checks --commit-diff
expkits-ci --python-format-check --cmake-format-check
expkits-ci --license-header --list-of-files src/main.cpp src/util.py
```

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
