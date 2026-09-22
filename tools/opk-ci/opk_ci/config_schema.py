################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import shutil
import subprocess
from pathlib import Path


def find_config_validator(project_root):
    """Locate the shared C++ descriptor validator."""
    project_root = Path(project_root)
    for candidate in (
        project_root / "tools/opk-config-check",
        project_root / "development/build/meson-out/opk-config-check",
    ):
        if candidate.is_file():
            return str(candidate)

    validator = shutil.which("opk-config-check")
    if validator:
        return validator
    raise FileNotFoundError(
        "opk-config-check is not available; build the development container first"
    )


def run_config_validator(project_root):
    """Run the repository CLI backed by production's per-document validator."""
    project_root = Path(project_root)
    return subprocess.run(
        [
            find_config_validator(project_root),
            "--root",
            str(project_root),
        ],
        cwd=project_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        check=False,
    )
