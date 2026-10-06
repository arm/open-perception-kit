# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
