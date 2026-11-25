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


import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort


def main():
    parser = argparse.ArgumentParser(
        description="Qualify the centered fixed-canvas bilinear recipe against a pinned export.")
    parser.add_argument("--model-descriptor", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    descriptor = json.loads(args.model_descriptor.read_text())
    model = (args.model_descriptor.parent / descriptor["modelFile"]).resolve()
    digest = hashlib.sha256(model.read_bytes()).hexdigest()
    if digest != descriptor["hfDownload"]["sha256"]:
        raise ValueError("Model does not match the descriptor's pinned SHA-256")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(model), sess_options=options, providers=["CPUExecutionProvider"])
    shape = session.get_inputs()[0].shape
    if len(shape) != 4 or shape[:2] != [1, 3] or not all(isinstance(x, int) and x > 0 for x in shape):
        raise ValueError("Qualification requires a fixed RGB NCHW export")
    canvas_height, canvas_width = shape[2:]
    bgr = cv2.imread(str(args.image))
    if bgr is None:
        raise ValueError("Cannot read the input image")
    height, width = bgr.shape[:2]
    gain = min(canvas_width / width, canvas_height / height)
    resized_width, resized_height = round(width * gain), round(height * gain)
    left, top = (canvas_width - resized_width) // 2, (canvas_height - resized_height) // 2
    # Ultralytics fixed-canvas recipe: auto=False, scale_fill=False, scaleup=True, center=True.
    canvas = np.full((canvas_height, canvas_width, 3), 114, np.uint8)
    canvas[top:top + resized_height, left:left + resized_width] = cv2.resize(
        bgr, (resized_width, resized_height), interpolation=cv2.INTER_LINEAR)
    tensor = np.ascontiguousarray(canvas[:, :, ::-1].transpose(2, 0, 1)[None], dtype=np.float32) / np.float32(255)
    output = session.run(None, {session.get_inputs()[0].name: tensor})[0]
    args.output.mkdir(parents=True, exist_ok=True)
    cv2.cvtColor(bgr, cv2.COLOR_BGR2BGRA).tofile(args.output / "second-model.bgra")
    tensor.tofile(args.output / "second-model-input.f32")
    output.tofile(args.output / "second-model-output.f32")
    receipt = dict(artifact=str(model), sha256=digest, shape=shape, source_width=width,
                   source_height=height, left=left, top=top, width=resized_width, height=resized_height,
                   output_shape=list(output.shape), opencv=cv2.__version__, onnxruntime=ort.__version__,
                   image_sha256=hashlib.sha256(args.image.read_bytes()).hexdigest())
    (args.output / "second-model.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
