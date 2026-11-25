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
import copy
import hashlib
import inspect
import json
import os
import resource
from pathlib import Path
from artifacts import ROOT, author_source


def main():
    parser = argparse.ArgumentParser(
        description="Export and qualify locally materialized YOLO-E graphs; consumes download-models.py outputs."
    )
    parser.add_argument(
        "--model-dir", type=Path, default=ROOT / "config/models/yoloe-v8s-onnx"
    )
    args = parser.parse_args()
    model_dir = args.model_dir.resolve()
    work = model_dir / "export"
    (model_dir / "export-report.record").unlink(missing_ok=True)
    with author_source(model_dir) as (source, receipts):
        import torch
        import torch.nn.functional as F
        import numpy as np
        import cv2
        import onnx
        import onnxruntime as ort
        from ultralytics import YOLOE
        from ultralytics.data.augment import LetterBox
        from ultralytics.models.yolo.yoloe.predict_vp import YOLOEVPSegPredictor
        from ultralytics.utils import ops

        torch.set_num_threads(2)
        model = YOLOE(str(work / "yoloe-v8s-seg.pt")).model.float().eval()
        names = [
            "person",
            "bus",
            "dog",
            "bicycle",
            "car",
            "cat",
            "train",
            "truck",
            "horse",
            "chair",
            "table",
            "bottle",
            "bird",
            "boat",
            "cow",
            "motorcycle",
        ]
        features = model.get_text_pe(names, cache_clip_model=True)
        text_model = model.clip_model

        # The text encoder plus RepRTA produces final conditioning. The detector receives
        # that conditioning through rank-3 vpe, without applying RepRTA again.
        class Text(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.encoder = text_model.model.text_encoder
                self.alignment = model.model[-1].reprta

            def forward(self, tokens):
                x = self.encoder(text_tokens=tokens, key_padding_mask=None)
                x = F.normalize(x, dim=-1).unsqueeze(0)
                return F.normalize(self.alignment(x), dim=-1)

        class Detector(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.net = copy.deepcopy(model)
                self.net.clip_model = None
                head = self.net.model[-1]
                head.export = True
                head.format = "onnx"
                head.dynamic = True

            def forward(self, image, conditioning):
                self.net.model[-1].nc = conditioning.shape[1]
                return self.net(image, vpe=conditioning)

        class Visual(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.net = copy.deepcopy(model)
                self.net.clip_model = None

            def forward(self, reference, regions):
                return self.net.get_visual_pe(reference, regions)

        def image_tensor(path):
            bgr = cv2.imread(str(path))
            assert bgr is not None
            resized = LetterBox((640, 640), auto=False)(image=bgr)
            x = (
                torch.from_numpy(
                    np.ascontiguousarray(resized[:, :, ::-1].transpose(2, 0, 1))
                )
                .float()
                .unsqueeze(0)
                / 255
            )
            return bgr, x

        bus, bus_tensor = image_tensor(source / "ultralytics/assets/bus.jpg")
        zidane, target = image_tensor(source / "ultralytics/assets/zidane.jpg")
        vp = YOLOEVPSegPredictor(overrides={"device": "cpu"})
        label = vp.process_box_label(
            bus,
            np.array([[40, 380, 240, 920]], np.float32),
            np.array([0]),
            LetterBox((640, 640), auto=False),
        )
        regions = label["visuals"].unsqueeze(0)
        reference = (
            torch.from_numpy(
                np.ascontiguousarray(label["img"][:, :, ::-1].transpose(2, 0, 1))
            )
            .float()
            .unsqueeze(0)
            / 255
        )
        np.testing.assert_array_equal(reference.numpy(), bus_tensor.numpy())
        text = Text().eval()
        detector = Detector().eval()
        visual = Visual().eval()
        tokens = text_model.tokenize(names)
        report = {
            "source": "40cd606cabdbe2b566d6f14a6b162c89206e9a1b",
            "contracts": {},
            "checks": [],
        }
        options = ort.SessionOptions()
        options.intra_op_num_threads = 2
        options.inter_op_num_threads = 1

        def export_graph(module, args, name, inputs, outputs, dynamic_axes):
            path = model_dir / (name + ".onnx")
            receipt_data = {
                "wrapper": inspect.getsource(type(module)),
                "source": report["source"],
                "dynamic_axes": dynamic_axes,
                "opset": 17,
                "dynamo": False,
                "torch": torch.__version__,
                "onnx": onnx.__version__,
                "weights": {},
                "artifacts": receipts,
                "export_script_sha256": hashlib.sha256(
                    Path(__file__).read_bytes()
                ).hexdigest(),
            }
            for artifact in ["yoloe-v8s-seg.pt", "mobileclip_blt.pt"]:
                with (work / artifact).open("rb") as stream:
                    receipt_data["weights"][artifact] = hashlib.file_digest(
                        stream, "sha256"
                    ).hexdigest()
            fingerprint = hashlib.sha256(
                json.dumps(receipt_data, sort_keys=True).encode()
            ).hexdigest()
            receipt = path.with_suffix(".receipt")
            if (
                not path.exists()
                or not receipt.exists()
                or json.loads(receipt.read_text()).get("fingerprint") != fingerprint
                or json.loads(receipt.read_text()).get("output_sha256")
                != hashlib.sha256(path.read_bytes()).hexdigest()
            ):
                with torch.no_grad():
                    torch.onnx.export(
                        module,
                        args,
                        str(path),
                        input_names=inputs,
                        output_names=outputs,
                        dynamic_axes=dynamic_axes,
                        opset_version=17,
                        dynamo=False,
                    )
                receipt.write_text(
                    json.dumps(
                        {
                            "fingerprint": fingerprint,
                            "inputs": receipt_data,
                            "output_sha256": hashlib.sha256(
                                path.read_bytes()
                            ).hexdigest(),
                        },
                        indent=2,
                    )
                )
            onnx.checker.check_model(str(path))
            session = ort.InferenceSession(
                str(path), sess_options=options, providers=["CPUExecutionProvider"]
            )
            report["contracts"][name] = {
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "bytes": path.stat().st_size,
                "inputs": [(x.name, x.type, x.shape) for x in session.get_inputs()],
                "outputs": [(x.name, x.type, x.shape) for x in session.get_outputs()],
            }
            print(name, report["contracts"][name], flush=True)
            return session

        def compare_outputs(name, expected, actual, atol=1e-3):
            diff = float(np.max(np.abs(expected - actual)))
            passed = bool(np.allclose(actual, expected, rtol=1e-3, atol=atol))
            item = {
                "name": name,
                "shape": list(actual.shape),
                "max_abs_error": diff,
                "initial_tolerance_pass": passed,
            }
            if actual.ndim == 3 and actual.shape[-1] == 8400:
                groups = actual.shape[1] - 36
                item["max_box_pixel_error"] = float(
                    np.max(np.abs(actual[:, :4] - expected[:, :4]))
                )
                item["max_score_error"] = float(
                    np.max(
                        np.abs(actual[:, 4: 4 + groups] - expected[:, 4: 4 + groups])
                    )
                )
                item["max_mask_coefficient_error"] = float(
                    np.max(np.abs(actual[:, 4 + groups:] - expected[:, 4 + groups:]))
                )
            report["checks"].append(item)
            print("PASS" if passed else "TOLERANCE_REVIEW", item, flush=True)
            if "max_box_pixel_error" in item:
                assert item["max_box_pixel_error"] < 0.1
                assert item["max_score_error"] < 1e-5
                assert item["max_mask_coefficient_error"] < 5e-4
            else:
                assert diff < atol

        # Acceptance thresholds fixed before the following rerun: model-input boxes <0.1px,
        # scores <1e-5, mask coefficients <5e-4, binary mask IoU >=0.995.
        def postprocess_check(name, expected, actual, count, threshold=0.25):
            ep = ops.non_max_suppression(
                expected[0].clone(), threshold, 0.7, nc=count, max_det=100
            )[0]
            ap = ops.non_max_suppression(
                torch.from_numpy(actual[0].copy()),
                threshold,
                0.7,
                nc=count,
                max_det=100,
            )[0]
            assert ep.shape == ap.shape, (name, ep.shape, ap.shape)
            assert torch.equal(ep[:, 5], ap[:, 5]), name
            box_error = float((ep[:, :4] - ap[:, :4]).abs().max()) if len(ep) else 0
            score_error = float((ep[:, 4] - ap[:, 4]).abs().max()) if len(ep) else 0
            assert box_error < 0.1 and score_error < 1e-5, (
                name,
                box_error,
                score_error,
            )
            ious = []
            if len(ep):
                em = ops.process_mask(
                    expected[1][2][0], ep[:, 6:], ep[:, :4], (640, 640), upsample=True
                ).bool()
                am = ops.process_mask(
                    torch.from_numpy(actual[1][0]),
                    ap[:, 6:],
                    ap[:, :4],
                    (640, 640),
                    upsample=True,
                ).bool()
                for e, a in zip(em, am):
                    union = (e | a).sum().item()
                    iou = (e & a).sum().item() / union if union else 1.0
                    assert iou >= 0.995, (name, iou)
                    ious.append(iou)
            item = {
                "name": name,
                "detections": ap[:, :6].tolist(),
                "max_post_nms_box_pixel_error": box_error,
                "max_post_nms_score_error": score_error,
                "mask_ious": ious,
            }
            report["checks"].append(item)
            print("POSTPROCESS", item, flush=True)

        with torch.no_grad():
            compare_outputs(
                "text-wrapper-vs-author", features.numpy(), text(tokens).numpy(), 2e-4
            )
            model.set_classes(names, features)
            author = model(target)
            wrapped = detector(target, features)
            compare_outputs(
                "detector-wrapper-vs-author", author[0].numpy(), wrapped[0].numpy()
            )
            compare_outputs(
                "prototype-wrapper-vs-author", author[1][2].numpy(), wrapped[1].numpy()
            )
            ts = export_graph(
                text,
                (tokens,),
                "text-encoder",
                ["tokens"],
                ["conditioning"],
                {"tokens": {0: "groups"}, "conditioning": {1: "groups"}},
            )
            ds = export_graph(
                detector,
                (target, features),
                "detector",
                ["image", "conditioning"],
                ["predictions", "prototypes"],
                {"conditioning": {1: "groups"}, "predictions": {1: "channels"}},
            )
            vs = export_graph(
                visual,
                (reference, regions),
                "visual-encoder",
                ["reference", "regions"],
                ["conditioning"],
                {"regions": {1: "groups"}, "conditioning": {1: "groups"}},
            )
            for count in [1, 2, 4, 16]:
                tok = text_model.tokenize(names[:count])
                feats = model.get_text_pe(names[:count], cache_clip_model=True)
                enc = ts.run(None, {"tokens": tok.numpy()})[0]
                compare_outputs(f"text-onnx-{count}", feats.numpy(), enc, 2e-4)
                model.set_classes(names[:count], feats)
                expected = model(target)
                actual = ds.run(None, {"image": target.numpy(), "conditioning": enc})
                compare_outputs(
                    f"detector-onnx-{count}", expected[0].numpy(), actual[0]
                )
                compare_outputs(
                    f"prototype-onnx-{count}", expected[1][2].numpy(), actual[1]
                )
                postprocess_check(f"text-postprocess-{count}", expected, actual, count)
                predictions = ops.non_max_suppression(
                    torch.from_numpy(actual[0]), 0.25, 0.7, nc=count, max_det=100
                )[0]
                report["checks"].append(
                    {
                        "name": f"detections-{count}",
                        "detections": predictions[:, :6].tolist(),
                    }
                )
            for count in [1, 2, 4, 16]:
                r = regions.repeat(1, count, 1, 1)
                expected = model.get_visual_pe(reference, r)
                actual = vs.run(
                    None, {"reference": reference.numpy(), "regions": r.numpy()}
                )[0]
                compare_outputs(f"visual-onnx-{count}", expected.numpy(), actual, 2e-4)
                model.set_classes([f"example-{i}" for i in range(count)], expected)
                author = model(target)
                actual_detection = ds.run(
                    None, {"image": target.numpy(), "conditioning": actual}
                )
                compare_outputs(
                    f"cross-image-detector-{count}",
                    author[0].numpy(),
                    actual_detection[0],
                )
                postprocess_check(
                    f"visual-postprocess-{count}", author, actual_detection, count, 0.1
                )
                predictions = ops.non_max_suppression(
                    torch.from_numpy(actual_detection[0]),
                    0.1,
                    0.7,
                    nc=count,
                    max_det=100,
                )[0]
                report["checks"].append(
                    {
                        "name": f"visual-detections-{count}",
                        "detections": predictions[:, :6].tolist(),
                    }
                )
            # Distinct reference categories, order reversal and different image aspect ratios.
            boxes = np.array([[40, 380, 240, 920], [20, 220, 800, 750]], np.float32)
            label2 = vp.process_box_label(
                bus, boxes, np.array([0, 1]), LetterBox((640, 640), auto=False)
            )
            r = label2["visuals"].unsqueeze(0)
            np.testing.assert_array_equal(label2["img"], label["img"])
            original = None
            for order in [[0, 1], [1, 0]]:
                ordered = r[:, order]
                expected = model.get_visual_pe(reference, ordered)
                encoded = vs.run(
                    None, {"reference": reference.numpy(), "regions": ordered.numpy()}
                )[0]
                compare_outputs(
                    f"distinct-visual-{order}", expected.numpy(), encoded, 2e-4
                )
                if original is None:
                    original = encoded
                else:
                    np.testing.assert_allclose(
                        encoded, original[:, order], rtol=1e-5, atol=1e-6
                    )
                model.set_classes([f"reference-{i}" for i in order], expected)
                author = model(target)
                actual = ds.run(
                    None, {"image": target.numpy(), "conditioning": encoded}
                )
                postprocess_check(
                    f"distinct-visual-postprocess-{order}", author, actual, 2, 0.1
                )
            report["reference_transform"] = {
                "reference_size": list(bus.shape[:2]),
                "target_size": list(zidane.shape[:2]),
                "boxes_xyxy": boxes.tolist(),
                "region_nonzero": [int((r[0, i] > 0).sum()) for i in range(2)],
                "input": "RGB float32 /255, LetterBox640 auto=False",
                "raster": "author process_box_label / LoadVisualPrompt 80x80",
            }
            # Distinct semantic queries on exactly the same media, same ORT detector session.
            for query in [["person"], ["bus"], ["dog"]]:
                enc = ts.run(None, {"tokens": text_model.tokenize(query).numpy()})[0]
                output = ds.run(
                    None, {"image": bus_tensor.numpy(), "conditioning": enc}
                )[0]
                predictions = ops.non_max_suppression(
                    torch.from_numpy(output), 0.25, 0.7, nc=1, max_det=100
                )[0]
                report["checks"].append(
                    {
                        "name": "changed-query",
                        "query": query,
                        "detections": predictions[:, :6].tolist(),
                    }
                )
                print("QUERY", query, predictions[:, :6].tolist(), flush=True)
        report["peak_process_rss_kib"] = resource.getrusage(
            resource.RUSAGE_SELF
        ).ru_maxrss
        temporary_report = model_dir / "export-report.record.partial"
        temporary_report.write_text(json.dumps(report, indent=2))
        os.replace(temporary_report, model_dir / "export-report.record")
        print("COMPLETE", flush=True)


if __name__ == "__main__":
    main()
