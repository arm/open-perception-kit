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

"""Apply dependency patches declared alongside their locked source archives."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

REPO = Path(__file__).resolve().parents[2]
REQUIREMENTS = REPO / "requirements"


def patch_path(root, name):
    path = Path(name)
    if path.is_absolute() or ".." in path.parts or path.parts[:1] != ("patches",) or path.suffix != ".patch":
        raise ValueError(f"Invalid dependency patch path: {name}")
    target = (root / path).resolve()
    if not target.is_relative_to(root.resolve() / "patches") or not target.is_file():
        raise ValueError(f"Missing or unsafe dependency patch: {target}")
    return target


def patch_inventory(lock, requirements=REQUIREMENTS):
    patches = []
    for source, record in lock.items():
        if not isinstance(record, dict):
            continue
        declared = record.get("patches", [])
        if not isinstance(declared, list):
            raise ValueError(f"{source}: patches must be an ordered list")
        seen = set()
        for entry in declared:
            if not isinstance(entry, dict) or not {"file", "reason"} <= entry.keys() <= {"file", "reason", "upstream"}:
                raise ValueError(f"{source}: each patch needs file, reason and optionally upstream")
            if any(not isinstance(value, str) or not value.strip() for value in entry.values()):
                raise ValueError(f"{source}: patch fields must be non-empty strings")
            path = patch_path(requirements, entry["file"])
            if path in seen:
                raise ValueError(f"{source}: duplicate patch {entry['file']}")
            seen.add(path)
            patches.append({"source": source, "file": entry["file"],
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return patches


def prepare_sources(lock, work_dir, requirements=REQUIREMENTS):
    sys.path.insert(0, str(REPO / "tools/perception"))
    from artifacts import acquire_artifact
    from sdk_config import LockedArtifact

    patches = patch_inventory(lock, requirements)
    sources = {}
    for name, record in lock.items():
        if not isinstance(record, dict):
            continue
        artifact = LockedArtifact(**{key: value for key, value in record.items() if key != "patches"})
        source_name = f"{artifact.name}-{artifact.version}"
        if any(Path(value).name != value or value in ("", ".", "..")
               for value in (artifact.filename, source_name)):
            raise ValueError(f"{name}: archive and source names must be plain filenames")
        with tarfile.open(acquire_artifact(artifact, work_dir)) as archive:
            archive.extractall(work_dir, filter="data")
        sources[name] = work_dir / source_name
        if not sources[name].is_dir() or not sources[name].resolve().is_relative_to(work_dir.resolve()):
            raise RuntimeError(f"Missing or unsafe source directory in {artifact.filename}: {source_name}")
    for entry in patches:
        source = sources[entry["source"]]
        content = patch_path(requirements, entry["file"]).read_bytes()
        if hashlib.sha256(content).hexdigest() != entry["sha256"]:
            raise RuntimeError(f"Dependency patch changed during build: {entry['file']}")
        # Temporary sources may be below an OPK checkout; do not discover its .git.
        environment = {key: value for key, value in os.environ.items()
                       if key not in {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE"}}
        environment["GIT_CEILING_DIRECTORIES"] = str(source.parent.resolve())
        print(f"Applying {entry['file']} to {entry['source']} {lock[entry['source']]['version']}", flush=True)
        subprocess.run(["git", "apply"], input=content, cwd=source, env=environment, check=True)
    return sources, patches


def stage_patches(patches, destination, requirements=REQUIREMENTS):
    (destination / "patches").mkdir(parents=True)
    for entry in patches:
        content = patch_path(requirements, entry["file"]).read_bytes()
        if hashlib.sha256(content).hexdigest() != entry["sha256"]:
            raise RuntimeError(f"Dependency patch changed during build: {entry['file']}")
        target = destination / entry["file"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    (destination / "patches.json").write_text(json.dumps(patches, indent=2) + "\n")


def validate_patch_files(directory, expected=None):
    patches = json.loads((directory / "patches.json").read_text())
    if not isinstance(patches, list) or (expected is not None and patches != expected):
        raise RuntimeError("Packaged dependency patches do not match the selected source")
    for entry in patches:
        if not isinstance(entry, dict) or set(entry) != {"source", "file", "sha256"}:
            raise ValueError("Invalid packaged dependency patch entry")
        path = patch_path(directory, entry["file"])
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise RuntimeError(f"Packaged dependency patch checksum mismatch: {entry['file']}")
    return tuple(entry["file"] for entry in patches)


def main():
    parser = argparse.ArgumentParser(description="Check dependency patches against their pinned source archives.")
    parser.add_argument("descriptors", nargs="+", type=Path)
    args = parser.parse_args()
    for descriptor in args.descriptors:
        lock = json.loads(descriptor.read_text())
        with tempfile.TemporaryDirectory(prefix="opk-dependency-patches-") as temporary:
            prepare_sources(lock, Path(temporary), descriptor.parent)
        print(f"OK: {descriptor}")


if __name__ == "__main__":
    main()
