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

"""Build the pinned Vulkan Graph and Tensor emulation layers."""

import argparse
import json
import os
from pathlib import Path
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
    parser.add_argument("destination", type=Path, help="New runtime prefix")
    destination = parser.parse_args().destination.absolute()
    if destination.exists() or destination.is_symlink():
        parser.error("Destination already exists; choose a new runtime prefix")
    lock = json.loads((REPO / "requirements/vulkan-emulation.json").read_text())
    lock["vulkan_headers"] = json.loads((REPO / "requirements/mlwl.json").read_text())["headers"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".vulkan-emulation-", dir=destination.parent) as temporary:
        work_dir = Path(temporary)
        staging_dir = work_dir / "prefix"
        sources = {}
        for name, record in lock.items():
            artifact = LockedArtifact(**record)
            with tarfile.open(acquire_artifact(artifact, work_dir)) as archive:
                archive.extractall(work_dir, filter="data")
            sources[name] = work_dir / f"{artifact.name}-{artifact.version}"

        build_dir = work_dir / "build"
        subprocess.run(
            [
                "cmake", "-G", "Ninja",
                "-S", str(sources["emulation"]),
                "-B", str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
                "-DCMAKE_INSTALL_LIBDIR=lib64",
                f"-DML_SDK_PACKAGE_VERSION=0.0.0+git.{lock['emulation']['version'][:8]}",
                "-DVMEL_TESTS_ENABLE=OFF",
                "-DVMEL_BUILD_DOCS=OFF",
                *[f"-D{name.upper()}_PATH={path}" for name, path in sources.items() if name != "emulation"],
            ],
            check=True,
        )
        subprocess.run(
            ["cmake", "--build", str(build_dir), "--parallel",
             os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "2")],
            check=True,
        )
        subprocess.run(["cmake", "--install", str(build_dir)], check=True)

        notices = staging_dir / "share/doc/vulkan-emulation"
        for name, source in sources.items():
            (notices / name).mkdir(parents=True)
            for license_path in (*source.glob("LICENSE*"), *source.glob("NOTICE*")):
                target = notices / name / license_path.name
                if license_path.is_dir():
                    shutil.copytree(license_path, target)
                else:
                    shutil.copy2(license_path, target)
        (notices / "sources.json").write_text(json.dumps(lock, indent=2) + "\n")
        staging_dir.rename(destination)
    print(f"Installed Vulkan emulation {lock['emulation']['version']} in {destination}")


if __name__ == "__main__":
    main()
