#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "bare"))

import compare_video_benchmark_summaries as compare  # noqa: E402
import prepare_video  # noqa: E402
import video_benchmark as bare  # noqa: E402


def runner_summary(runner: str, fps: float) -> dict:
    source = prepare_video.source_manifest(Path("video.mp4"))
    args = SimpleNamespace(model="model.onnx", video=Path("video.mp4"))
    doc = bare.summary_document(args, source, prepare_video.SOURCE_FRAME_COUNT, 204_000 / fps, 1.0)
    doc["runner"] = runner
    doc["timing"]["pipeline_fps"] = fps
    if runner.startswith("pek"):
        doc["inputs"]["model"] = ""
        doc["inputs"]["opchain"] = "opchain.json"
    return doc


class YoloVideoBenchmarkTest(unittest.TestCase):
    def test_summary_uses_inter_result_throughput(self) -> None:
        source = prepare_video.source_manifest(Path("video.mp4"))
        args = SimpleNamespace(model="model.onnx", video=Path("video.mp4"))

        doc = bare.summary_document(args, source, prepare_video.SOURCE_FRAME_COUNT, 2_040.0, 10.0)

        self.assertEqual(doc["timing"]["measured_frames"], 204)
        self.assertEqual(doc["timing"]["pipeline_fps"], 100.0)

    def test_comparison_and_report_use_higher_is_better_fps(self) -> None:
        comparison = compare.build_comparison(
            runner_summary("bare-ultralytics-video", 10.0),
            runner_summary("pek-pipeline-video", 12.0),
        )
        self.assertAlmostEqual(comparison["fps"]["delta_percent"], 20.0)

        with tempfile.TemporaryDirectory() as tmpdir:
            runs = Path(tmpdir)
            for index, (bare_fps, pek_fps) in enumerate(((10.0, 12.0), (20.0, 18.0), (15.0, 16.0)), start=1):
                run = runs / f"run-{index:02d}"
                run.mkdir()
                doc = compare.build_comparison(
                    runner_summary("bare-ultralytics-video", bare_fps),
                    runner_summary("pek-pipeline-video", pek_fps),
                )
                (run / "comparison.json").write_text(json.dumps(doc), encoding="utf-8")

            report = compare.build_report(runs)

        self.assertEqual(report["median_fps"]["bare_fps"], 15.0)
        self.assertEqual(report["median_fps"]["pek_fps"], 16.0)


if __name__ == "__main__":
    unittest.main()
