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

"""Copy Docker build inputs without descriptor-referenced model artifacts."""

import argparse
import json
import shutil
from pathlib import Path


def copy_without_models(source: Path, destination: Path) -> None:
    source = source.resolve()
    destination = destination.resolve()
    excluded = set()
    for descriptor in source.rglob("*.json"):
        try:
            model = json.loads(descriptor.read_text(encoding="utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            # Other build inputs include JSONC and potentially binary .json files.
            continue
        if isinstance(model, dict) and isinstance(model.get("modelFile"), str):
            artifact = (descriptor.parent / model["modelFile"]).resolve()
            if artifact.is_relative_to(destination):
                artifact = (source / artifact.relative_to(destination)).resolve()
            excluded.update((artifact, artifact.with_name(artifact.name + ".part")))

    shutil.copytree(
        source, destination, symlinks=True,
        ignore=lambda directory, names: [
            name for name in names if (Path(directory) / name).resolve() in excluded
        ],
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    copy_without_models(args.source, args.destination)
