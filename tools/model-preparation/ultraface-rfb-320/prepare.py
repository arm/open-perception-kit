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

"""Convert the pinned FP32 UltraFace export into the existing OPK parser contract."""

import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import numpy as np
import onnx
from onnx import helper, numpy_helper


def priors(feature_map_centres):
    result = []
    for width, height, stride, sizes in (
        (40, 30, 8, (10, 16, 24)), (20, 15, 16, (32, 48)),
        (10, 8, 32, (64, 96)), (5, 4, 64, (128, 192, 256)),
    ):
        scale_x, scale_y = (width, height) if feature_map_centres else (320 / stride, 240 / stride)
        for y in range(height):
            for x in range(width):
                for size in sizes:
                    result.append(((x + .5) / scale_x, (y + .5) / scale_y, size / 320, size / 240))
    return np.clip(np.asarray(result, dtype=np.float32), 0, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="New output directory; never overwrites an existing export")
    parser.add_argument("--model-converter", required=True, type=Path)
    args = parser.parse_args()
    source = json.loads((Path(__file__).parent / "source/model.json").read_text())["hfDownload"]
    digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
    if digest != source["sha256"]:
        parser.error("Source ONNX SHA-256 does not match the pinned model")
    if importlib.metadata.version("torch-mlir") != "20260401.769":
        parser.error("Expected torch-mlir 20260401.769")
    converter = args.model_converter.resolve()
    version = json.loads(subprocess.check_output([str(converter), "--version"], text=True))
    if version["version"] != "0.11.0":
        parser.error("Expected model-converter 0.11.0")
    destination = args.output.absolute()
    if destination.exists() or destination.is_symlink():
        parser.error("Output already exists; choose a new output directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as temporary:
        output = Path(temporary) / "bundle"
        output.mkdir()
        model = onnx.load(args.source)
        original, opk = priors(True), priors(False)
        correction = np.zeros((1, 4420, 4), dtype=np.float32)
        correction[0, :, :2] = (original[:, :2] - opk[:, :2]) / (.1 * opk[:, 2:])
        # The existing parser expects probabilities and image/stride anchor centres.
        names = [value.name for value in model.graph.output]
        model.graph.initializer.append(numpy_helper.from_array(correction, "opk_prior_correction"))
        model.graph.node.extend((
            helper.make_node("Softmax", [names[0]], ["opk_scores"], axis=-1),
            helper.make_node("Add", [names[1], "opk_prior_correction"], ["opk_locations"]),
        ))
        model.graph.output[0].name = "opk_scores"
        model.graph.output[1].name = "opk_locations"
        onnx.checker.check_model(model, full_check=True)
        onnx.save(model, output / "reference.onnx")
        tools = Path(sys.executable).parent
        commands = [
            [str(tools / "torch-mlir-import-onnx"), "reference.onnx",
             "--data-prop", "--clear-domain", "-o", "torch.mlir"],
            [str(tools / "torch-mlir-opt"), "torch.mlir",
             "--torch-onnx-to-torch-backend-pipeline", "--canonicalize", "-o", "backend.mlir"],
            [str(tools / "torch-mlir-opt"), "backend.mlir",
             "--pass-pipeline=builtin.module(torch-backend-to-tosa-backend-pipeline{require-full-tosa-conversion})",
             "-o", "model.tosa.mlir"],
            [str(converter), "--input", "model.tosa.mlir", "--output", "ultraface-rfb-320.vgf"],
        ]
        for command in commands:
            subprocess.run(command, cwd=output, check=True)
        config = Path(__file__).resolve().parents[3] / "config/models/ultraface-rfb-320-mlwl"
        for name in ("model.json", "opchain.json"):
            shutil.copyfile(config / name, output / name)
        manifest = {
            "source": source, "model_converter": version, "torch_mlir": "20260401.769",
            "numpy": np.__version__, "onnx": onnx.__version__,
            "adaptation": "Softmax over classes and prior-centre correction; original weights unchanged",
            "input": [1, 3, 240, 320], "outputs": [[1, 4420, 2], [1, 4420, 4]],
            "precision": "Float32", "license": "MIT",
            "validation": "Run validate.py; conversion alone is not runtime validation",
        }
        (output / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
        files = ("ultraface-rfb-320.vgf", "reference.onnx", "model.tosa.mlir",
                 "model.json", "opchain.json", "provenance.json")
        (output / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n" for name in files
        ))
        output.rename(destination)


if __name__ == "__main__":
    main()
