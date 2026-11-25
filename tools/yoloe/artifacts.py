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


"""Read the download owner's verified local inputs; never fetch artifacts here."""

import hashlib
import json
import os
import sys
import tarfile
import tempfile
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_COMMIT = "40cd606cabdbe2b566d6f14a6b162c89206e9a1b"


@contextmanager
def author_source(model_dir):
    manifest = json.loads((ROOT / "config/model-assets.json").read_text())
    receipts = {}
    prefix = "yoloe-v8s-onnx/"
    for name, entry in manifest.items():
        if not name.startswith(prefix):
            continue
        path = model_dir / name.removeprefix(prefix)
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != entry["sha256"]:
            raise ValueError(f"Artifact hash mismatch: {path}")
        receipts[name] = entry
    with tempfile.TemporaryDirectory(prefix="opk-yoloe-source-") as directory:
        with tarfile.open(model_dir / "export/source.tar.gz") as archive:
            archive.extractall(directory, filter="data")
        (source,) = Path(directory).iterdir()
        sys.path[:0] = [
            str(source),
            str(source / "third_party/CLIP"),
            str(source / "third_party/ml-mobileclip"),
        ]
        os.environ["YOLO_CONFIG_DIR"] = str(Path(directory) / "settings")
        os.chdir(model_dir / "export")
        yield source, receipts
