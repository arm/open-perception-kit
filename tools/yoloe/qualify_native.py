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
import importlib.util
import json
from pathlib import Path
import numpy as np
import cv2
import torch
from artifacts import ROOT, author_source


def main():
    parser = argparse.ArgumentParser(
        description="Generate independent author numerical oracles for native query_model_tests."
    )
    parser.add_argument(
        "--model-dir", type=Path, default=ROOT / "config/models/yoloe-v8s-onnx"
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    model_dir = args.model_dir.resolve()
    out = args.output.resolve()
    work = model_dir / "export"
    with author_source(model_dir) as (source, receipts):
        from ultralytics import YOLOE
        from ultralytics.data.augment import LetterBox
        from ultralytics.utils import ops
        import open_clip.tokenizer as oracle

        out.mkdir(parents=True, exist_ok=True)
        module_path = model_dir / "scripts/tokenizer.py"
        spec = importlib.util.spec_from_file_location("opk_tokenizer", module_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        tokenizer = module.create_tokenizer(oracle.default_bpe())
        corpus = [
            "person",
            "A red bus",
            "piros autó",
            "红色汽车",
            "犬",
            "a café 🚲",
            "I’m looking for cars",
            "a\\b",
            "&amp;amp; bird",
            "FranÃ§ais",
            "person\x1c bus",
            "person\x1d bus",
            "person\x1e bus",
            "person\x1f bus",
            "\t person\n\u00a0 bus",
            "<start_of_text> person <end_of_text>",
            "a " * 75,
        ]
        expected = oracle.tokenize(corpus).tolist()
        actual = tokenizer(corpus)
        assert expected == actual
        try:
            tokenizer(["a " * 76])
            raise AssertionError("overflow was truncated")
        except ValueError:
            pass
        (out / "tokenizer-parity.json").write_text(
            json.dumps(
                {"cases": corpus, "exact": True, "overflow_rejected": True},
                ensure_ascii=False,
            )
        )
        for filename in [
            "model.json",
            "model-text-encoder.json",
            "model-visual-encoder.json",
        ]:
            descriptor = json.loads((model_dir / filename).read_text())
            descriptor["modelFile"] = str(model_dir / descriptor["modelFile"])
            (out / filename).write_text(json.dumps(descriptor, indent=2))
        for filename in ["opchain.json", "opchain-visual.json"]:
            descriptor = json.loads((model_dir / filename).read_text())
            for operation in descriptor["ops"]:
                attributes = operation["attributes"]
                if "modelDescriptor" in attributes:
                    attributes["modelDescriptor"] = str(
                        model_dir / attributes["modelDescriptor"]
                    )
            (out / filename).write_text(json.dumps(descriptor, indent=2))
        bgr = cv2.imread(str(source / "ultralytics/assets/bus.jpg"))
        bgra = cv2.cvtColor(bgr, cv2.COLOR_BGR2BGRA)
        bgra.tofile(out / "target.bgra")
        letter = LetterBox((640, 640), auto=False)(image=bgr)
        tensor = (
            torch.from_numpy(
                np.ascontiguousarray(letter[:, :, ::-1].transpose(2, 0, 1))
            )
            .float()
            .unsqueeze(0)
            / 255
        )
        torch.set_num_threads(2)
        model = YOLOE(str(work / "yoloe-v8s-seg.pt")).model.float().eval()
        report = {
            "width": bgr.shape[1],
            "height": bgr.shape[0],
            "cases": [],
            "pixel_cases": [],
        }
        for i, names in enumerate(
            [["person"], ["bus"], ["dog"], ["bus", "person"], ["person"]]
        ):
            with torch.no_grad():
                features = model.get_text_pe(names, cache_clip_model=True)
                model.set_classes(names, features)
                prediction = model(tensor)
                detections = ops.non_max_suppression(
                    prediction[0].clone(), 0.25, 0.7, nc=len(names), max_det=100
                )[0]
                masks = (
                    ops.process_mask(
                        prediction[1][2][0],
                        detections[:, 6:],
                        detections[:, :4],
                        (640, 640),
                        upsample=True,
                    )
                    .numpy()
                    .astype(np.uint8)
                    if len(detections)
                    else []
                )
                scaled = detections[:, :6].clone()
                ops.scale_boxes((640, 640), scaled[:, :4], bgr.shape[:2])
                ratio = min(640 / bgr.shape[1], 640 / bgr.shape[0])
                w, h = round(bgr.shape[1] * ratio), round(bgr.shape[0] * ratio)
                left, top = (640 - w) // 2, (640 - h) // 2
                for n, mask in enumerate(masks):
                    cv2.resize(
                        mask[top: top + h, left: left + w],
                        (bgr.shape[1], bgr.shape[0]),
                        interpolation=cv2.INTER_NEAREST,
                    ).tofile(out / f"mask-{i}-{n}.bin")
                report["cases"].append(
                    {
                        "names": names,
                        "boxes": scaled.tolist(),
                        "mask_prefix": f"mask-{i}-",
                    }
                )
        for i, (w, h) in enumerate([(7, 11), (319, 241), (810, 1080), (1281, 719)]):
            rgb = np.random.default_rng(i).integers(0, 256, (h, w, 3), dtype=np.uint8)
            bgra = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGRA)
            expected = (
                LetterBox((640, 640), auto=False)(image=rgb)
                .transpose(2, 0, 1)
                .astype(np.float32)
                / 255
            )
            rgb.tofile(out / f"pixels-{i}.rgb")
            bgra.tofile(out / f"pixels-{i}.bgra")
            expected.tofile(out / f"pixels-{i}.f32")
            report["pixel_cases"].append({"width": w, "height": h, "index": i})
        (out / "expected.json").write_text(json.dumps(report, indent=2))
        print("NATIVE_FIXTURES_COMPLETE", len(corpus), len(report["cases"]))

        from ultralytics.models.yolo.yoloe.predict_vp import YOLOEVPSegPredictor

        vp = YOLOEVPSegPredictor(overrides={"device": "cpu"})
        ref0 = cv2.imread(str(source / "ultralytics/assets/bus.jpg"))
        ref1 = cv2.resize(ref0, (601, 801), interpolation=cv2.INTER_LINEAR)
        refs = [ref0, ref1]
        for i, r in enumerate(refs):
            cv2.cvtColor(r, cv2.COLOR_BGR2RGB).tofile(out / f"reference-{i}.rgb")
        target = cv2.imread(str(source / "ultralytics/assets/zidane.jpg"))
        cv2.cvtColor(target, cv2.COLOR_BGR2BGRA).tofile(out / "visual-target.bgra")
        x = (
            torch.from_numpy(
                np.ascontiguousarray(
                    LetterBox((640, 640), auto=False)(image=target)[
                        :, :, ::-1
                    ].transpose(2, 0, 1)
                )
            )
            .float()
            .unsqueeze(0)
            / 255
        )
        person = [40 / 810, 380 / 1080, 240 / 810, 920 / 1080]
        busbox = [10 / 810, 220 / 1080, 800 / 810, 750 / 1080]
        report = {
            "width": target.shape[1],
            "height": target.shape[0],
            "references": [
                {
                    "width": r.shape[1],
                    "height": r.shape[0],
                    "file": f"reference-{i}.rgb",
                }
                for i, r in enumerate(refs)
            ],
            "cases": [],
        }
        for ci, queries in enumerate(
            [
                [("person", 0, person)],
                [("person", 0, person), ("bus", 0, busbox)],
                [("bus", 0, busbox), ("person", 0, person)],
                [("person", 1, person), ("bus", 0, busbox)],
                [("person", 0, person)],
            ]
        ):
            features = torch.empty(1, len(queries), 512)
            with torch.no_grad():
                for ri, ref in enumerate(refs):
                    ids = [i for i, q in enumerate(queries) if q[1] == ri]
                    if not ids:
                        continue
                    boxes = np.array(
                        [queries[i][2] for i in ids], np.float64
                    ) * np.array([ref.shape[1], ref.shape[0]] * 2)
                    label = vp.process_box_label(
                        ref,
                        boxes.astype(np.float32),
                        np.arange(len(ids)),
                        LetterBox((640, 640), auto=False),
                    )
                    image = (
                        torch.from_numpy(
                            np.ascontiguousarray(
                                label["img"][:, :, ::-1].transpose(2, 0, 1)
                            )
                        )
                        .float()
                        .unsqueeze(0)
                        / 255
                    )
                    features[:, ids] = model.get_visual_pe(
                        image, label["visuals"].unsqueeze(0)
                    )
                features.numpy().tofile(out / f"visual-features-{ci}.f32")
                model.set_classes([q[0] for q in queries], features)
                prediction = model(x)
                detections = ops.non_max_suppression(
                    prediction[0].clone(), 0.25, 0.7, nc=len(queries), max_det=100
                )[0]
                masks = (
                    ops.process_mask(
                        prediction[1][2][0],
                        detections[:, 6:],
                        detections[:, :4],
                        (640, 640),
                        upsample=True,
                    )
                    .numpy()
                    .astype(np.uint8)
                    if len(detections)
                    else []
                )
                scaled = detections[:, :6].clone()
                ops.scale_boxes((640, 640), scaled[:, :4], target.shape[:2])
                ratio = min(640 / target.shape[1], 640 / target.shape[0])
                w, h = round(target.shape[1] * ratio), round(target.shape[0] * ratio)
                left, top = (640 - w) // 2, (640 - h) // 2
                for n, mask in enumerate(masks):
                    cv2.resize(
                        mask[top: top + h, left: left + w],
                        (target.shape[1], target.shape[0]),
                        interpolation=cv2.INTER_NEAREST,
                    ).tofile(out / f"visual-mask-{ci}-{n}.bin")
                report["cases"].append(
                    {
                        "names": [q[0] for q in queries],
                        "queries": queries,
                        "boxes": scaled.tolist(),
                        "mask_prefix": f"visual-mask-{ci}-",
                        "encodings": len(set(q[1] for q in queries)),
                    }
                )
        (out / "expected-visual.json").write_text(json.dumps(report, indent=2))
        print("Native visual fixtures complete")


if __name__ == "__main__":
    main()
