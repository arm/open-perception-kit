#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT_PATH = Path(__file__).with_name("publish_yolo_benchmark_pages.py")


def import_publish_module():
    spec = importlib.util.spec_from_file_location("publish_yolo_benchmark_pages", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


publish = import_publish_module()


def comparison(bare_ms: float = 10.0, pek_ms: float = 15.0) -> dict:
    row = {
        "bare_ms": bare_ms,
        "pek_ms": pek_ms,
        "delta_ms": pek_ms - bare_ms,
        "ratio": pek_ms / bare_ms,
        "delta_percent": (pek_ms / bare_ms - 1.0) * 100.0,
    }
    return {
        "measurement": {"timed_region": "preloaded_image_to_postprocess_result_ready"},
        "inputs": {"image_count": 2, "imgsz": 320, "device": "cpu"},
        "timing_delta": {"per_image_ms": {metric: dict(row) for metric in publish.METRICS}},
    }


class TestPublishYoloBenchmarkPages(unittest.TestCase):
    def test_find_yolo_artifact_accepts_artifact_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir) / "artifacts" / "yolo-benchmark"
            (root / "runs").mkdir(parents=True)

            self.assertEqual(publish.find_yolo_artifact(root), root)

    def test_write_report_page_renders_table_chart_and_links(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "manual" / "123"
            target.mkdir(parents=True)
            runs = [
                {"name": "run-01", "path": Path("run-01"), "comparison": comparison()},
                {"name": "run-02", "path": Path("run-02"), "comparison": comparison(11.0, 18.0)},
            ]

            publish.write_report_page(target, site_dir, "Manual run 123", "Manual", runs)

            content = (target / "index.html").read_text(encoding="utf-8")
            self.assertIn("Performance Plots", content)
            self.assertIn("class=\"metric-chart\"", content)
            self.assertIn("run-01", content)
            self.assertIn("50.0%", content)
            self.assertIn("runs/run-01/comparison.json", content)
            self.assertIn('href="../../index.html"', content)
            self.assertIn('href="../../report-index.css"', content)

    def test_select_target_supports_manual_reports(self) -> None:
        with patch.dict(os.environ, {"UPSTREAM_RUN_ID": "123"}):
            target, pr_number, title = publish.select_target(Path("site"), "repo", "workflow_dispatch", "feature/test")

            self.assertEqual(target, Path("site") / "yolo-benchmark" / "manual" / "123")
            self.assertEqual(pr_number, "")
            self.assertEqual(title, "Manual run 123")

    def test_write_selected_artifacts_skips_prediction_jsonl(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            artifact = root / "artifact"
            run_dir = artifact / "runs" / "run-01"
            (run_dir / "bare").mkdir(parents=True)
            (run_dir / "pek").mkdir()
            for relative in (
                Path("comparison.json"),
                Path("comparison.md"),
                Path("bare") / "benchmark_summary.json",
                Path("pek") / "benchmark_summary.json",
                Path("bare") / "predictions.jsonl",
            ):
                path = run_dir / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("data", encoding="utf-8")

            target = root / "site"
            publish.write_selected_artifacts(artifact, target, [{"name": "run-01", "path": run_dir}])

            self.assertTrue((target / "runs" / "run-01" / "comparison.json").is_file())
            self.assertFalse((target / "runs" / "run-01" / "bare" / "predictions.jsonl").exists())

    def test_local_artifact_copy_ignores_cache_dirs(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            ignore = publish.local_artifact_ignore(root)

            ignored = ignore(str(root), ["images.tsv", "runs", ".venv", "pek-build", "comparison.json"])

            self.assertEqual(ignored, {".venv", "pek-build", "comparison.json"})
            self.assertEqual(ignore(str(root / "runs" / "run-01"), ["predictions.jsonl", "comparison.json"]),
                             {"predictions.jsonl"})


if __name__ == "__main__":
    unittest.main()
