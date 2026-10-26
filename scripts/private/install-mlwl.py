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

"""Build and install pinned MLWL sources and matching Vulkan headers."""

import argparse
import json
import os
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
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "aarch64"):
        parser.error("Supported platforms: Linux x86_64 and aarch64")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".mlwl-", dir=destination.parent) as temporary:
        work_dir = Path(temporary)
        staging_dir = work_dir / "prefix"
        staging_dir.mkdir()

        sources = {}
        for name in ("library", "vgf", "flatbuffers", "headers"):
            artifact = LockedArtifact(**lock[name])
            with tarfile.open(acquire_artifact(artifact, work_dir)) as archive:
                archive.extractall(work_dir, filter="data")
            sources[name] = work_dir / f"{artifact.name}-{artifact.version}"

        build_dir = work_dir / "build"
        subprocess.run(
            [
                "cmake", "-G", "Ninja",
                "-S", str(sources["library"]),
                "-B", str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
                "-DCMAKE_INSTALL_LIBDIR=lib64",
                "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                f"-DML_SDK_PACKAGE_VERSION={lock['version']}",
                f"-DVULKAN_HEADERS_PATH={sources['headers']}",
                f"-DML_SDK_VGF_LIB_PATH={sources['vgf']}",
                f"-DFLATBUFFERS_PATH={sources['flatbuffers']}",
                "-DML_WORKLOAD_LIB_BUILD_SHARED=ON",
                "-DVULKAN_HEADERS_ENABLE_INSTALL=ON",
                "-DVULKAN_HEADERS_ENABLE_TESTS=OFF",
                "-DVULKAN_HEADERS_ENABLE_MODULE=OFF",
            ],
            check=True,
        )
        subprocess.run(
            ["cmake", "--build", str(build_dir), "--parallel",
             os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "2")],
            check=True,
        )
        subprocess.run(["cmake", "--install", str(build_dir)], check=True)
        subprocess.run(["cmake", "--install", str(build_dir / "vulkan-headers")], check=True)
        shutil.copytree(
            sources["headers"] / "LICENSES",
            staging_dir / "share/doc/Vulkan-Headers/LICENSES",
        )
        for name in ("library", "vgf"):
            shutil.copytree(sources[name] / "LICENSES", staging_dir / f"share/doc/{name}/LICENSES")
        shutil.copy2(sources["flatbuffers"] / "LICENSE", staging_dir / "share/doc/flatbuffers-LICENSE")
        staging_dir.rename(destination)
    print(f"Installed MLWL {lock['version']} and Vulkan-Headers in {destination}")


if __name__ == "__main__":
    main()
