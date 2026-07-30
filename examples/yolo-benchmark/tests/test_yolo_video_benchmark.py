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
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "bare"))

import compare_video_benchmark_summaries as compare  # noqa: E402
import prepare_video  # noqa: E402
import video_benchmark as bare  # noqa: E402


class FakeResult:
    def __init__(self) -> None:
        self.serialization_count = 0

    def to_json(self) -> str:
        self.serialization_count += 1
        return "{}"


class FakeModel:
    def __init__(self) -> None:
        self.results: list[FakeResult] = []
        self.predict_count = 0

    def predict(self, **kwargs: object) -> list[FakeResult]:
        self.predict_count += 1
        results = [FakeResult() for _ in range(len(kwargs["source"]))]
        self.results.extend(results)
        return results


def runner_summary(runner: str, fps: float) -> dict:
    source = prepare_video.source_manifest(Path("video.mp4"))
    args = SimpleNamespace(model="model.onnx", video=Path("video.mp4"))
    doc = bare.summary_document(
        args,
        source,
        prepare_video.SOURCE_FRAME_COUNT,
        204_000 / fps,
        1.0,
        2.0,
    )
    doc["runner"] = runner
    doc["timing"]["pipeline_fps"] = fps
    if runner.startswith("pek"):
        doc["inputs"]["model"] = ""
        doc["inputs"]["opchain"] = "opchain.json"
    return doc


class YoloVideoBenchmarkTest(unittest.TestCase):
    def test_preloaded_video_source_yields_single_frame_batches(self) -> None:
        source = bare.preloaded_video_source(
            ["first", "second"],
            object,
            lambda **values: SimpleNamespace(**values),
        )

        self.assertEqual(list(source), [(["frame-000001"], ["first"], [""]),
                                        (["frame-000002"], ["second"], [""])])
        self.assertTrue(source.source_type.stream)
        self.assertEqual(source.bs, 1)

    def test_runner_reuses_preloaded_frames_for_warmup_and_measurement(self) -> None:
        source = prepare_video.source_manifest(Path("video.mp4"))
        model = FakeModel()
        frames = [object()] * prepare_video.SOURCE_FRAME_COUNT

        with patch.object(
            bare.time,
            "perf_counter",
            side_effect=range(prepare_video.SOURCE_FRAME_COUNT * 2),
        ):
            total_frames, elapsed_ms = bare.benchmark_model(model, frames, source)

        self.assertEqual(model.predict_count, 2)
        self.assertEqual(len(model.results), prepare_video.SOURCE_FRAME_COUNT * 2)
        self.assertEqual(total_frames, prepare_video.SOURCE_FRAME_COUNT)
        self.assertEqual(elapsed_ms, 204_000.0)
        self.assertTrue(all(result.serialization_count == 1 for result in model.results))

    def test_summary_uses_preloaded_video_contract(self) -> None:
        source = prepare_video.source_manifest(Path("video.mp4"))
        args = SimpleNamespace(model="model.onnx", video=Path("video.mp4"))

        doc = bare.summary_document(args, source, prepare_video.SOURCE_FRAME_COUNT, 2_040.0, 10.0, 20.0)

        self.assertEqual(doc["schema"], bare.SUMMARY_SCHEMA_ID)
        self.assertFalse(doc["measurement"]["decode_included"])
        self.assertFalse(doc["measurement"]["source_color_conversion_included"])
        self.assertTrue(doc["measurement"]["preloaded_frames"])
        self.assertEqual(doc["measurement"]["warmup_video_passes"], 1)
        self.assertEqual(doc["timing"]["preload_ms"], 20.0)
        self.assertEqual(doc["timing"]["measured_frames"], 204)
        self.assertEqual(doc["timing"]["pipeline_fps"], 100.0)

    def test_summary_rejects_incomplete_measured_pass(self) -> None:
        source = prepare_video.source_manifest(Path("video.mp4"))
        args = SimpleNamespace(model="model.onnx", video=Path("video.mp4"))

        with self.assertRaisesRegex(ValueError, "processed 204 frames"):
            bare.summary_document(args, source, prepare_video.SOURCE_FRAME_COUNT - 1, 1.0, 1.0, 1.0)

    def test_comparison_rejects_invalid_measurement_contract(self) -> None:
        bare_summary = runner_summary("bare-ultralytics-video", 10.0)
        pek_summary = runner_summary("pek-pipeline-video", 12.0)
        for summary in (bare_summary, pek_summary):
            summary["measurement"]["technique"] = "different"

        with self.assertRaisesRegex(ValueError, "measurement.technique"):
            compare.build_comparison(bare_summary, pek_summary)

    def test_comparison_and_report_use_higher_is_better_fps(self) -> None:
        comparison = compare.build_comparison(
            runner_summary("bare-ultralytics-video", 10.0),
            runner_summary("pek-pipeline-video", 12.0),
        )
        self.assertEqual(comparison["schema"], "expkits_yolo_video_comparison.v3")
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

        self.assertEqual(report["schema"], "expkits_yolo_video_report.v3")
        self.assertEqual(report["median_fps"]["bare_fps"], 15.0)
        self.assertEqual(report["median_fps"]["pek_fps"], 16.0)


if __name__ == "__main__":
    unittest.main()
