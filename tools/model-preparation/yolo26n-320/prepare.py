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

"""Prepare pinned YOLO26n FP32 heads, preserving nearest-floor upsampling."""

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


def preserve_nearest_floor(model):
    shapes = {"/model.11/Resize": (10, 10), "/model.14/Resize": (20, 20)}
    nodes = []
    replaced = set()
    for node in model.graph.node:
        if node.op_type != "Resize":
            nodes.append(node)
            continue
        attributes = {value.name: helper.get_attribute_value(value) for value in node.attribute}
        if (node.name not in shapes or attributes.get("mode") != b"nearest" or
                attributes.get("coordinate_transformation_mode") != b"asymmetric" or
                attributes.get("nearest_mode") != b"floor"):
            raise ValueError("Unexpected Resize in pinned model")
        height, width = shapes[node.name]
        prefix = node.name + "/opk"
        for suffix, extent in (("rows", height), ("columns", width)):
            model.graph.initializer.append(numpy_helper.from_array(
                np.repeat(np.arange(extent, dtype=np.int64), 2), prefix + "/" + suffix))
        # Torch-MLIR's nearest resize rounds half-up; ONNX here requires floor.
        # Exact 2x replication avoids changing the learned network or sampling.
        nodes.extend((
            helper.make_node("Gather", [node.input[0], prefix + "/rows"], [prefix + "/expanded"], axis=2),
            helper.make_node("Gather", [prefix + "/expanded", prefix + "/columns"], list(node.output), axis=3),
        ))
        replaced.add(node.name)
    if replaced != shapes.keys():
        raise ValueError("Expected both nearest-floor resize operations")
    del model.graph.node[:]
    model.graph.node.extend(nodes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="New output directory")
    parser.add_argument("--model-converter", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads((Path(__file__).parent / "source/model.json").read_text())["hfDownload"]
    if hashlib.sha256(args.source.read_bytes()).hexdigest() != source["sha256"]:
        parser.error("Source ONNX SHA-256 does not match the pinned model")
    if importlib.metadata.version("torch-mlir") != "20260401.769":
        parser.error("Expected torch-mlir 20260401.769")
    converter = args.model_converter.resolve()
    version = json.loads(subprocess.check_output([str(converter), "--version"], text=True))
    if version["version"] != "0.11.0":
        parser.error("Expected model-converter 0.11.0")
    destination = args.output.absolute()
    if destination.exists() or destination.is_symlink():
        parser.error("Output already exists; choose a new directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as temporary:
        output = Path(temporary) / "bundle"
        output.mkdir()
        model = onnx.load(args.source)
        onnx.utils.extract_model(str(args.source), str(output / "reference.onnx"),
                                 [model.graph.input[0].name], ["/model.23/Transpose_output_0"])
        model = onnx.load(output / "reference.onnx")
        preserve_nearest_floor(model)
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
            [str(converter), "--input", "model.tosa.mlir", "--output", "yolo26n-320.vgf"],
        ]
        for command in commands:
            subprocess.run(command, cwd=output, check=True)
        config = Path(__file__).resolve().parents[3] / "config/models/yolo26n-320-mlwl"
        for name in ("model.json", "opchain.json"):
            shutil.copyfile(config / name, output / name)
        manifest = {
            "source": source, "model_converter": version, "torch_mlir": "20260401.769",
            "onnx": onnx.__version__, "numpy": np.__version__, "license": "AGPL-3.0",
            "adaptation": "Host-side TopK; exact 2x nearest-floor replication; weights and decoded boxes unchanged",
            "input": [1, 3, 320, 320], "output": [1, 2100, 84], "precision": "Float32",
            "validation": "Compare real OPK detections against the full original ONNX; conversion alone is not validation",
        }
        (output / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
        files = ("yolo26n-320.vgf", "reference.onnx", "model.tosa.mlir",
                 "model.json", "opchain.json", "provenance.json")
        (output / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n" for name in files))
        output.rename(destination)


if __name__ == "__main__":
    main()
