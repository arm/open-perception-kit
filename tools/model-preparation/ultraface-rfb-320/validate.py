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

"""Compare real OPK/MLWL detections against the original FP32 ONNX model."""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import numpy as np
import onnxruntime as ort
from PIL import Image

from prepare import priors

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "generated/open_perception_kit/python/src"))


def reference_detections(logits, offsets, confidence_threshold, iou_threshold):
    scores = np.exp(logits[0] - logits[0].max(axis=-1, keepdims=True))
    scores = (scores / scores.sum(axis=-1, keepdims=True))[:, 1]
    anchors = priors(True)
    centre = anchors[:, :2] + offsets[0, :, :2] * .1 * anchors[:, 2:]
    size = anchors[:, 2:] * np.exp(offsets[0, :, 2:] * .2)
    corners = np.clip(np.concatenate((centre - size / 2, centre + size / 2), axis=-1), 0, 1)
    candidates = np.flatnonzero(scores >= confidence_threshold)
    candidates = candidates[np.argsort(-scores[candidates])]
    selected = []
    while candidates.size:
        current = candidates[0]
        selected.append(current)
        others = candidates[1:]
        intersection = np.maximum(0, np.minimum(corners[current, 2:], corners[others, 2:]) -
                                  np.maximum(corners[current, :2], corners[others, :2])).prod(axis=-1)
        area = np.prod(corners[:, 2:] - corners[:, :2], axis=-1)
        union = area[current] + area[others] - intersection
        overlap = np.divide(intersection, union, out=np.zeros_like(intersection), where=union > 0)
        candidates = others[overlap < iou_threshold]
    boxes = corners[selected] * np.array([320, 240, 320, 240])
    boxes[:, 2:] -= boxes[:, :2]
    return boxes, scores[selected]


def main():
    from open_perception_kit.packet import decode
    from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, help="OPK meson-out directory")
    parser.add_argument("bundle", type=Path)
    parser.add_argument("source", type=Path, help="Pinned original ONNX, not reference.onnx")
    parser.add_argument("image", type=Path, help="Face photograph for the positive case")
    args = parser.parse_args()
    manifest = json.loads((args.bundle / "provenance.json").read_text())
    opchain = json.loads((args.bundle / "opchain.json").read_text())
    postprocess = next(op["attributes"] for op in opchain["ops"]
                       if op["id"] == "opk-std-ops/GenericPostprocess")
    if hashlib.sha256(args.source.read_bytes()).hexdigest() != manifest["source"]["sha256"]:
        raise ValueError("Original ONNX checksum does not match the provenance")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    session = ort.InferenceSession(str(args.source), sess_options=options, providers=["CPUExecutionProvider"])
    photo = np.asarray(Image.open(args.image).convert("RGB").resize((320, 240), Image.Resampling.BILINEAR))
    cases = (("photo", photo), ("black", np.zeros_like(photo)), ("photo-repeat", photo))
    report = {"vgf_sha256": hashlib.sha256((args.bundle / "ultraface-rfb-320.vgf").read_bytes()).hexdigest(),
              "image_sha256": hashlib.sha256(args.image.read_bytes()).hexdigest(),
              "scope": "Original ONNX vs actual MLWL/OPK FrameResults; not hardware performance or dataset accuracy",
              "cases": []}
    expected = []
    frames = []
    for case, rgb in cases:
        tensor = np.ascontiguousarray((rgb.astype(np.float32).transpose(2, 0, 1)[None] - 127) / 128)
        raw_outputs = session.run(None, {session.get_inputs()[0].name: tensor})
        if not all(np.isfinite(value).all() for value in raw_outputs):
            raise ValueError(f"{case}: non-finite ONNX reference output")
        expected_boxes, expected_scores = reference_detections(
            *raw_outputs, postprocess["confidenceThreshold"], postprocess["iouThreshold"])
        if case != "black" and not len(expected_scores):
            raise ValueError("The positive reference case must contain faces")
        expected.append((expected_boxes, expected_scores))
        bgra = np.concatenate((rgb[:, :, ::-1], np.full((240, 320, 1), 255, dtype=np.uint8)), axis=-1)
        frames.append(bgra.tobytes())
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        (directory / "input.bgra").write_bytes(b"".join(frames))
        output = directory / "frames.ndjson"
        environment = os.environ.copy()
        for variable in ("LD_LIBRARY_PATH", "GST_PLUGIN_PATH"):
            environment[variable] = os.pathsep.join(
                filter(None, (str(args.build.resolve()), environment.get(variable))))
        start = time.monotonic()
        # A host supervisor owns the deadline and device recovery; MLWL cannot cancel in-flight work.
        subprocess.run([
            "gst-launch-1.0", "-e", "filesrc", f"location={directory / 'input.bgra'}", "!",
            "rawvideoparse", "format=bgra", "width=320", "height=240", "framerate=1/1", "!",
            "opkinfer", f"opchain-path={args.bundle.resolve() / 'opchain.json'}", "!",
            "opkcomm", "method=file", f"file-name={output}", "!", "fakesink", "sync=false",
        ], env=environment, check=True)
        report["wall_seconds"] = time.monotonic() - start
        records = [json.loads(line) for line in output.read_text().splitlines()]
        if len(records) != len(cases):
            raise ValueError(f"Expected {len(cases)} packets, got {len(records)}")
        for record, (case, _), (expected_boxes, expected_scores) in zip(records, cases, expected):
            packet = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
            if not packet.valid():
                raise ValueError(f"{case}: {packet.error()}")
            detections = [d for layer in packet.for_each(BoxDetectionsT) for d in layer.detections]
            if len(detections) != len(expected_scores):
                raise ValueError(f"{case}: expected {len(expected_scores)} faces, got {len(detections)}")
            boxes = np.array([[d.box.x, d.box.y, d.box.width, d.box.height] for d in detections]).reshape(-1, 4)
            scores = np.array([d.confidence for d in detections])
            np.testing.assert_allclose(boxes, expected_boxes, atol=.01, rtol=1e-4, equal_nan=False)
            np.testing.assert_allclose(scores, expected_scores, atol=1e-5, rtol=1e-4, equal_nan=False)
            report["cases"].append({
                "case": case, "faces": len(expected_scores),
                "max_box_error_pixels": float(np.abs(boxes - expected_boxes).max(initial=0)),
                "max_confidence_error": float(np.abs(scores - expected_scores).max(initial=0)),
            })
    (args.bundle / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
