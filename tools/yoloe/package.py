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


"""Create a local, self-contained prototype model directory from qualified graphs."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path
from artifacts import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model-dir", type=Path, default=ROOT / "config/models/yoloe-v8s-onnx"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.model_dir.resolve()
    destination = args.output.resolve()
    if destination.is_relative_to(source):
        parser.error("Output must be outside the source model directory")
    if destination.exists():
        parser.error("Output must be a new directory")
    report = json.loads((source / "export-report.record").read_text())
    graphs = ["text-encoder", "visual-encoder", "detector"]
    for name in graphs:
        receipt = json.loads((source / (name + ".receipt")).read_text())
        with (source / (name + ".onnx")).open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if (
            digest != report["contracts"][name]["sha256"]
            or digest != receipt["output_sha256"]
        ):
            raise ValueError(f"Graph differs from qualified export: {name}")
    manifest = json.loads((ROOT / "config/model-assets.json").read_text())
    vocabulary = "bpe_simple_vocab_16e6.txt.gz"
    if (
        hashlib.sha256((source / vocabulary).read_bytes()).hexdigest()
        != manifest["yoloe-v8s-onnx/" + vocabulary]["sha256"]
    ):
        raise ValueError("Tokenizer vocabulary differs from qualified artifact")
    destination.mkdir(parents=True)
    try:
        for filename in [
            "model.json",
            "model-text-encoder.json",
            "model-visual-encoder.json",
            "opchain.json",
            "opchain-visual.json",
            "README.md",
            vocabulary,
            "export-report.record",
        ]:
            shutil.copy2(source / filename, destination / filename)
        for name in graphs:
            for extension in [".onnx", ".receipt"]:
                shutil.copy2(
                    source / (name + extension), destination / (name + extension)
                )
        shutil.copytree(
            source / "scripts",
            destination / "scripts",
            ignore=shutil.ignore_patterns("__pycache__"),
        )
    except BaseException:
        shutil.rmtree(destination)
        raise
    print(f"Local prototype model directory: {destination}")


if __name__ == "__main__":
    main()
