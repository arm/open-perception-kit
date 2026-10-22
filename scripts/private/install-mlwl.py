#!/usr/bin/env python3
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

"""Install the pinned Linux MLWL library and matching Vulkan headers."""

import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/perception"))
from artifacts import acquire_artifact  # noqa: E402
from sdk_config import LockedArtifact  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, help="New dependency prefix")
    args = parser.parse_args()
    destination = args.destination.absolute()
    if destination.exists() or destination.is_symlink():
        parser.error("Destination already exists; choose a new dependency prefix")
    lock = json.loads((REPO / "requirements/mlwl.json").read_text())
    if platform.system() != "Linux" or platform.machine() not in lock["linux"]:
        parser.error("Supported platforms: Linux x86_64 and aarch64")
    library = LockedArtifact(**lock["linux"][platform.machine()])
    headers = LockedArtifact(**lock["headers"])
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".mlwl-", dir=destination.parent) as temporary:
        work_dir = Path(temporary)
        staging_dir = work_dir / "prefix"
        staging_dir.mkdir()

        library_archive = acquire_artifact(library, work_dir)
        with tarfile.open(library_archive) as archive:
            archive.extractall(staging_dir, filter="data")

        headers_archive = acquire_artifact(headers, work_dir)
        with tarfile.open(headers_archive) as archive:
            archive.extractall(work_dir, filter="data")

        headers_source = work_dir / f"Vulkan-Headers-{headers.version}"
        headers_build = work_dir / "headers-build"
        subprocess.run(
            [
                "cmake",
                "-S", str(headers_source),
                "-B", str(headers_build),
                f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
                "-DVULKAN_HEADERS_ENABLE_TESTS=OFF",
                "-DVULKAN_HEADERS_ENABLE_MODULE=OFF",
            ],
            check=True,
        )
        subprocess.run(["cmake", "--install", str(headers_build)], check=True)
        shutil.copytree(
            headers_source / "LICENSES",
            staging_dir / "share/doc/Vulkan-Headers/LICENSES",
        )
        staging_dir.rename(destination)
    print(f"Installed MLWL {library.version} and Vulkan-Headers in {destination}")


if __name__ == "__main__":
    main()
