#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bare = load_module("bare_benchmark", ROOT / "bare" / "benchmark.py")
compare = load_module("compare_benchmark_summaries", ROOT / "compare_benchmark_summaries.py")
prepare = load_module("prepare_dataset", ROOT / "prepare_dataset.py")


def summary_doc(avg_ms: float = 10.0) -> dict[str, Any]:
    return {
        "schema": compare.SUMMARY_SCHEMA_ID,
        "runner": "runner",
        "measurement": compare.MEASUREMENT_CONSTS | {"warmup_images": 1},
        "inputs": {
            "model": "m.onnx",
            "opchain": "o.json",
            "image_list": "images.tsv",
            "image_count": 2,
            "image_set_fingerprint": "abc",
            "imgsz": 320,
            "device": "cpu",
        },
        "outputs": {"predictions_jsonl": "predictions.jsonl", "timings_jsonl": "timings.jsonl"},
        "timing": {
            "load_ms": 10.0,
            "preload_ms": 20.0,
            "loop_wall_ms": 30.0,
            "per_image_ms": {
                "count": 2,
                "avg_ms": avg_ms,
                "p50_ms": 9.0,
                "p75_ms": 10.0,
                "p95_ms": 11.0,
                "p99_ms": 12.0,
                "min_ms": 8.0,
                "max_ms": 12.0,
            },
        },
    }


class BareBenchmarkTest(unittest.TestCase):
    def test_percentile_and_image_list_parsing(self) -> None:
        self.assertEqual(bare.percentile([1.0, 2.0, 3.0], 0.50), 2.0)
        self.assertEqual(bare.percentile([1.0, 2.0, 3.0, 4.0], 0.75), 3.0)
        self.assertEqual(bare.percentile([1.0, 2.0, 3.0], 0.95), 3.0)
        with tempfile.TemporaryDirectory() as tmp:
            image_list = Path(tmp) / "images.tsv"
            image_list.write_text(
                "# image_set_fingerprint=sha256:test\n42\timages/a.jpg\n#skip\n7\timages/b.jpg\n", encoding="utf-8")
            self.assertEqual(bare.read_image_list(image_list), ("sha256:test",
                             [("42", "images/a.jpg"), ("7", "images/b.jpg")]))

    def test_write_summary_uses_schema_shape(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image_list = Path(tmp) / "images.tsv"
            args = argparse.Namespace(
                summary=Path(tmp) / "summary.json",
                output=Path(tmp) / "predictions.jsonl",
                model="model.onnx",
                images=image_list,
            )
            bare.write_summary(args, 0, "empty", 0, 1.0, 2.0, 3.0, [])
            doc = json.loads(args.summary.read_text(encoding="utf-8"))
            self.assertEqual(doc["measurement"], bare.MEASUREMENT_CONSTS | {"warmup_images": 0})
            self.assertEqual(doc["inputs"]["image_set_fingerprint"], "empty")
            self.assertEqual(doc["outputs"]["timings_jsonl"], str(Path(tmp) / "timings.jsonl"))


class CompareBenchmarksTest(unittest.TestCase):
    def test_comparison_delta_table(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bare_path = Path(tmp) / "bare.json"
            pek_path = Path(tmp) / "pek.json"
            bare_path.write_text(json.dumps(summary_doc()), encoding="utf-8")
            pek_path.write_text(json.dumps(summary_doc(avg_ms=15.0)), encoding="utf-8")

            comparison = compare.build_comparison(compare.load_summary(bare_path), compare.load_summary(pek_path))
            self.assertEqual(comparison["timing_delta"]["per_image_ms"]["avg_ms"]["ratio"], 1.5)
            self.assertEqual(comparison["timing_delta"]["per_image_ms"]["avg_ms"]["delta_percent"], 50.0)
            self.assertEqual(comparison["timing_delta"]["per_image_ms"]["p75_ms"]["delta_ms"], 0.0)

    def test_comparison_handles_zero_baseline_metric(self) -> None:
        bare_summary = summary_doc()
        pek_summary = summary_doc()
        bare_summary["timing"]["per_image_ms"]["min_ms"] = 0.0
        comparison = compare.build_comparison(bare_summary, pek_summary)
        self.assertIsNone(comparison["timing_delta"]["per_image_ms"]["min_ms"]["ratio"])

    def test_comparison_rejects_different_image_sets(self) -> None:
        bare_summary = summary_doc()
        pek_summary = summary_doc()
        pek_summary["inputs"]["image_set_fingerprint"] = "different"
        with self.assertRaisesRegex(ValueError, "image_set_fingerprint"):
            compare.build_comparison(bare_summary, pek_summary)


class PrepareDatasetTest(unittest.TestCase):
    def test_write_image_list_from_existing_dataset(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            coco_dir = Path(tmp) / "coco"
            image_dir = coco_dir / "val2017"
            annotation_dir = coco_dir / "annotations"
            image_dir.mkdir(parents=True)
            annotation_dir.mkdir()
            (image_dir / "000000000001.jpg").write_bytes(b"jpg")
            (annotation_dir / "instances_val2017.json").write_text(
                json.dumps({"images": [{"id": 1, "file_name": "000000000001.jpg"}]}),
                encoding="utf-8",
            )

            output = Path(tmp) / "images.tsv"
            prepare.write_image_list(coco_dir, output, 0)
            lines = output.read_text(encoding="utf-8").splitlines()
            self.assertTrue(lines[0].startswith("# image_set_fingerprint=sha256:"))
            self.assertEqual(lines[1], f"1\t{(image_dir / '000000000001.jpg').resolve()}")

    def test_extract_rejects_zip_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / "bad.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("../escape.txt", "x")

            with self.assertRaises(zipfile.BadZipFile):
                prepare.extract(archive, root / "out")
            self.assertFalse((root / "escape.txt").exists())


if __name__ == "__main__":
    unittest.main()
