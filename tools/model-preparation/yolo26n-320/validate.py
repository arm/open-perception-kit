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

"""Prepare independent ONNX expectations, then check real OPK FrameResults."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import sys
import tempfile

import numpy as np

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "generated/open_perception_kit/python/src"))


def prepare(args):
    import onnxruntime as ort
    from PIL import Image, ImageOps

    pinned = json.loads((Path(__file__).parent / "source/model.json").read_text())["hfDownload"]
    if hashlib.sha256(args.source.read_bytes()).hexdigest() != pinned["sha256"]:
        raise ValueError("Original ONNX checksum does not match the pinned source")
    destination = args.output.absolute()
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    original = ort.InferenceSession(str(args.source), sess_options=options, providers=["CPUExecutionProvider"])
    photo = np.asarray(ImageOps.pad(Image.open(args.image).convert("RGB"), (320, 320),
                                    method=Image.Resampling.BILINEAR, color=(114, 114, 114)))
    expected = []
    frames = []
    for name, rgb in (("photo", photo), ("black", np.zeros_like(photo)), ("photo-repeat", photo)):
        tensor = np.ascontiguousarray(rgb.astype(np.float32).transpose(2, 0, 1)[None] / 255)
        rows = original.run(None, {original.get_inputs()[0].name: tensor})[0][0]
        if rows.shape != (300, 6) or not np.isfinite(rows).all():
            raise ValueError(f"{name}: expected finite ONNX output with shape (300, 6), got {rows.shape}")
        selected = rows[rows[:, 4] >= .4].copy()
        if name == "black":
            if len(selected):
                raise ValueError("Expected no detections on the black frame")
        elif not len(selected):
            raise ValueError("Choose a photograph with positive detections")
        selected[:, :4] = np.clip(selected[:, :4], 0, 319)
        selected[:, 2:4] -= selected[:, :2]
        expected.append({"case": name, "detections": selected.tolist()})
        alpha = np.full((320, 320, 1), 255, dtype=np.uint8)
        frames.append(np.concatenate((rgb[:, :, ::-1], alpha), axis=-1).tobytes())
    report = {"source_sha256": pinned["sha256"],
              "image_sha256": hashlib.sha256(args.image.read_bytes()).hexdigest(), "cases": expected}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as temporary:
        output = Path(temporary) / "cases"
        output.mkdir()
        (output / "photo-black-photo.bgra").write_bytes(b"".join(frames))
        (output / "expected.json").write_text(json.dumps(report, indent=2) + "\n")
        output.rename(destination)
    print(f"Prepared photo / black / photo: {[len(case['detections']) for case in expected]}")


def check(args):
    from open_perception_kit.packet import decode
    from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT

    records = [json.loads(line) for line in args.frames.read_text().splitlines()]
    reference = json.loads(args.expected.read_text())
    expected = reference["cases"]
    if len(records) != 3 or len(expected) != 3:
        raise ValueError(f"Expected 3 packets and 3 reference cases, got {len(records)} and {len(expected)}")
    report = []
    for record, case in zip(records, expected):
        packet = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
        if not packet.valid():
            raise ValueError(f"{case['case']}: {packet.error()}")
        detections = [d for layer in packet.for_each(BoxDetectionsT) for d in layer.detections]
        actual = np.array([[d.box.x, d.box.y, d.box.width, d.box.height, d.confidence, d.classId]
                           for d in detections]).reshape(-1, 6)
        target = np.array(case["detections"]).reshape(-1, 6)
        if actual.shape != target.shape or not np.isfinite(actual).all():
            raise ValueError(
                f"{case['case']}: expected finite detections with shape {target.shape}, got {actual.shape}")
        np.testing.assert_array_equal(actual[:, 5], target[:, 5])
        np.testing.assert_allclose(actual[:, :4], target[:, :4], atol=.01, rtol=1e-4, equal_nan=False)
        np.testing.assert_allclose(actual[:, 4], target[:, 4], atol=1e-5, rtol=1e-4, equal_nan=False)
        report.append({"case": case["case"], "detections": len(detections),
                       "max_box_error_pixels": float(np.abs(actual[:, :4] - target[:, :4]).max(initial=0)),
                       "max_confidence_error": float(np.abs(actual[:, 4] - target[:, 4]).max(initial=0))})
    print(json.dumps({"result": "PASS", "frames": len(records), "source_sha256": reference["source_sha256"],
                      "image_sha256": reference["image_sha256"], "cases": report}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    preparation = commands.add_parser("prepare", help="Create photo/black/photo and original ONNX expectations")
    preparation.add_argument("source", type=Path)
    preparation.add_argument("image", type=Path)
    preparation.add_argument("output", type=Path, help="New output directory")
    comparison = commands.add_parser("check", help="Compare packets collected from the actual MLWL pipeline")
    comparison.add_argument("expected", type=Path)
    comparison.add_argument("frames", type=Path)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args)
    else:
        check(args)


if __name__ == "__main__":
    main()
