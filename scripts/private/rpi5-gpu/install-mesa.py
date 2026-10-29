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

"""Build the experimental V3D userspace driver inside the RPi5 runtime image."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tools/perception"))
from artifacts import acquire_artifact  # noqa: E402
from sdk_config import LockedArtifact  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, help="New driver prefix inside the image")
    destination = parser.parse_args().destination.absolute()
    if destination.exists() or destination.is_symlink():
        parser.error("Destination already exists; choose a new driver prefix")
    lock = json.loads((REPO / "requirements/rpi5-mesa.json").read_text())
    artifact = LockedArtifact(**lock)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".mesa-", dir=destination.parent) as temporary:
        work = Path(temporary)
        with tarfile.open(acquire_artifact(artifact, work)) as archive:
            archive.extractall(work, filter="data")
        source = work / f"{artifact.name}-{artifact.version}"
        patch = Path(__file__).with_name("mesa.patch")
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=source, check=True)
        build = work / "build"
        env = {**os.environ, "PYTHONPATH": "/usr/lib/python3/dist-packages"}
        subprocess.run([
            "meson", "setup", str(build), str(source), f"--prefix={destination}", "--libdir=lib",
            "--buildtype=release", "-Db_ndebug=false", "-Dgallium-drivers=",
            "-Dvulkan-drivers=broadcom", "-Dplatforms=", "-Dglx=disabled", "-Degl=disabled",
            "-Dgbm=disabled", "-Dgles1=disabled", "-Dgles2=disabled", "-Dopengl=false",
            "-Dllvm=disabled", "-Dvideo-codecs=", "-Dbuild-tests=false", "-Dvulkan-layers=",
            "-Dxmlconfig=disabled",
        ], env=env, check=True)
        subprocess.run(["meson", "compile", "-C", str(build), "-j",
                        os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "2")], env=env, check=True)
        subprocess.run(["meson", "install", "-C", str(build), "--no-rebuild",
                        "--destdir", str(work / "staging")], env=env, check=True)
        staging = work / "staging" / destination.relative_to("/")
        notices = staging / "share/doc/mesa"
        notices.mkdir(parents=True)
        shutil.copy2(source / "docs/license.rst", notices)
        shutil.copy2(patch, notices)
        (notices / "source.json").write_text(json.dumps(lock, indent=2) + "\n")
        staging.rename(destination)
    print(f"Installed experimental Mesa {artifact.version} in {destination}")


if __name__ == "__main__":
    main()
