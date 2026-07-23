#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from html.parser import HTMLParser
from pathlib import Path
from unittest.mock import patch


SCRIPT_PATH = Path(__file__).with_name("publish_yolo_benchmark_pages.py")
OVERLAY_SCRIPT_PATH = Path(__file__).with_name("restore_dataset_overlay.py")


def import_script(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


publish = import_script(SCRIPT_PATH, "publish_yolo_benchmark_pages")
overlay = import_script(OVERLAY_SCRIPT_PATH, "restore_dataset_overlay")


class LinkParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []
        self.ids = []
        self.links = []
        self.aria_controls = []
        self.thumbnails = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.append(attrs["id"])
        if "aria-controls" in attrs:
            self.aria_controls.append(attrs["aria-controls"])
        if "data-thumbnail" in attrs:
            self.thumbnails.append(attrs["data-thumbnail"])
        if tag == "a":
            self.links.append(attrs)
            if "href" in attrs:
                self.hrefs.append(attrs["href"])


def comparison(bare_ms: float = 10.0,
               pek_ms: float = 15.0,
               metrics: tuple[str, ...] = publish.RUN_METRICS) -> dict:
    row = {
        "bare_ms": bare_ms,
        "pek_ms": pek_ms,
        "delta_ms": pek_ms - bare_ms,
        "ratio": pek_ms / bare_ms,
        "delta_percent": (pek_ms / bare_ms - 1.0) * 100.0,
    }
    return {
        "measurement": {"timed_region": "preloaded_image_to_postprocess_result_ready"},
        "inputs": {
            "image_count": 2,
            "imgsz": 320,
            "device": "cpu",
            "bare_model": "config/models/yolov11/yolo11n-fp32-320.onnx",
            "pek_opchain": "config/models/yolov11/opchain.json",
        },
        "timing_delta": {"per_image_ms": {metric: dict(row) for metric in metrics}},
    }


def video_comparison(bare_fps: float = 10.0, pek_fps: float = 12.0) -> dict:
    ratio = pek_fps / bare_fps
    return {
        "schema": publish.VIDEO_COMPARISON_SCHEMA,
        "measurement": {
            "timed_region": "first_serialized_result_ready_to_last_serialized_result_ready",
        },
        "inputs": {
            "source_frame_count": 205,
            "source_width": 1920,
            "source_height": 1080,
            "source_fps": 30.0,
            "video_sha256": "abc123",
            "imgsz": 320,
            "device": "cpu",
            "bare_model": "model.onnx",
            "pek_opchain": "opchain.json",
        },
        "fps": {
            "bare_fps": bare_fps,
            "pek_fps": pek_fps,
            "delta_fps": pek_fps - bare_fps,
            "ratio": ratio,
            "delta_percent": (ratio - 1.0) * 100.0,
        },
    }


def report_run(name: str, bare_ms: float = 10.0, pek_ms: float = 15.0) -> dict:
    return {"name": name, "path": Path(name), "comparison": comparison(bare_ms, pek_ms), "image_timings": []}


class TestPublishYoloBenchmarkPages(unittest.TestCase):
    def test_find_yolo_artifact_accepts_artifact_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir) / "artifacts" / "yolo-benchmark"
            (root / "runs").mkdir(parents=True)

            self.assertEqual(publish.find_yolo_artifact(root), root)

    def test_load_report_runs_sorts_generated_comparisons(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            artifact = Path(tmpdir)
            for name, bare_ms, pek_ms in (("run-02", 12.0, 18.0), ("run-01", 10.0, 15.0)):
                run_dir = artifact / "runs" / name
                run_dir.mkdir(parents=True)
                (run_dir / "comparison.json").write_text(json.dumps(comparison(bare_ms, pek_ms)), encoding="utf-8")

            runs = publish.load_report_runs(artifact)

            self.assertEqual([run["name"] for run in runs], ["run-01", "run-02"])
            self.assertEqual(publish.metric_value(runs[1], "p75_ms", "pek_ms"), 18.0)

    def test_load_report_runs_loads_per_image_timings(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            artifact = Path(tmpdir)
            run_dir = artifact / "runs" / "run-01"
            (run_dir / "bare").mkdir(parents=True)
            (run_dir / "pek").mkdir()
            (artifact / "images.tsv").write_text(
                "# image_set_fingerprint=sha256:abcdef123456\n"
                "139\t/tmp/000000000139.jpg\n",
                encoding="utf-8",
            )
            (run_dir / "comparison.json").write_text(json.dumps(comparison()), encoding="utf-8")
            (run_dir / "bare" / "timings.jsonl").write_text(
                json.dumps({"image_index": 1, "image_id": "139", "width": 640, "height": 426,
                            "wall_ms": 10.0, "preprocess_ms": 1.0, "inference_ms": 8.0,
                            "postprocess_ms": 1.0}) + "\n",
                encoding="utf-8",
            )
            (run_dir / "pek" / "timings.jsonl").write_text(
                json.dumps({"image_index": 1, "image_id": "139", "width": 640, "height": 426,
                            "wall_ms": 15.0, "preprocess_ms": 2.0, "inference_ms": 12.0,
                            "postprocess_ms": 1.0}) + "\n",
                encoding="utf-8",
            )

            runs = publish.load_report_runs(artifact)

            self.assertEqual(runs[0]["image_timings"][0]["image_id"], "139")
            self.assertEqual(runs[0]["image_timings"][0]["image_file"], "000000000139.jpg")
            self.assertEqual(runs[0]["image_set_fingerprint"], "sha256:abcdef123456")
            self.assertEqual(runs[0]["image_timings"][0]["bare"]["inference_ms"], 8.0)
            self.assertEqual(runs[0]["image_timings"][0]["pek"]["inference_ms"], 12.0)

    def test_median_delta_includes_p75_metric(self) -> None:
        runs = [report_run("run-01", 10.0, 15.0), report_run("run-02", 12.0, 18.0)]

        p75 = publish.median_delta(runs, "p75_ms")

        self.assertEqual(p75["bare_ms"], 11.0)
        self.assertEqual(p75["pek_ms"], 16.5)
        self.assertEqual(p75["delta_ms"], 5.5)
        self.assertEqual(p75["ratio"], 1.5)

    def test_image_stage_profile_uses_per_image_median(self) -> None:
        runs = [
            {"image_timings": [
                {"image_index": 1, "image_id": "139",
                 "bare": {"preprocess_ms": 10.0}, "pek": {"preprocess_ms": 5.0}},
                {"image_index": 2, "image_id": "285",
                 "bare": {"preprocess_ms": 20.0}, "pek": {"preprocess_ms": 8.0}},
            ]},
            {"image_timings": [
                {"image_index": 1, "image_id": "139",
                 "bare": {"preprocess_ms": 12.0}, "pek": {"preprocess_ms": 7.0}},
                {"image_index": 2, "image_id": "285",
                 "bare": {"preprocess_ms": 22.0}, "pek": {"preprocess_ms": 10.0}},
            ]},
        ]

        profile = publish.image_stage_profile(runs, "preprocess_ms")

        self.assertEqual(profile[0]["image_index"], 1)
        self.assertEqual(profile[0]["bare_ms"], 11.0)
        self.assertEqual(profile[0]["pek_ms"], 6.0)
        self.assertEqual(profile[1]["image_id"], "285")

    def test_write_report_page_generates_index(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "manual" / "123"
            target.mkdir(parents=True)
            (target / "images.tsv").write_text(
                "# image_set_fingerprint=sha256:abcdef1234567890\n139\t/tmp/000000000139.jpg\n",
                encoding="utf-8",
            )

            publish.write_report_page(
                target,
                site_dir,
                "Manual run 123",
                "Manual",
                [{
                    **report_run("run-01"),
                    "image_set_fingerprint": "sha256:abcdef1234567890",
                    "image_timings": [{
                        "image_index": 1,
                        "image_id": "139",
                        "image_file": "000000000139.jpg",
                        "width": 640,
                        "height": 426,
                        "bare": {"wall_ms": 10.0, "preprocess_ms": 1.0, "inference_ms": 8.0,
                                 "postprocess_ms": 1.0},
                        "pek": {"wall_ms": 15.0, "preprocess_ms": 2.0, "inference_ms": 12.0,
                                "postprocess_ms": 1.0},
                    }],
                }, report_run("run-02", 12.0, 18.0)],
            )

            html = (target / "index.html").read_text(encoding="utf-8")
            links = LinkParser()
            links.feed(html)
            image_href = "../../../yolo-performance-datasets/coco-val2017-abcdef123456/images/000000000139.jpg"

            section_ids = [item for item in links.ids if item in {
                "section-information", "summary", "runs", "dataset-analysis",
            }]
            self.assertEqual(section_ids, ["section-information", "summary", "runs", "dataset-analysis"])
            self.assertEqual(links.aria_controls.count("section-runs"), 1)
            self.assertIn("images.tsv", links.hrefs)
            self.assertIn(image_href, links.hrefs)
            self.assertGreaterEqual(links.hrefs.count(image_href), 2)
            image_links = [
                link for link in links.links if link.get("href") == image_href
            ]
            self.assertTrue(any(link.get("target") == "_blank" for link in image_links))
            self.assertIn(image_href, links.thumbnails)

    def test_write_report_page_generates_video_fps_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "manual" / "123"
            target.mkdir(parents=True)
            runs = [
                {"name": "run-01", "path": Path("run-01"), "comparison": video_comparison(10.0, 12.0)},
                {"name": "run-02", "path": Path("run-02"), "comparison": video_comparison(11.0, 13.0)},
            ]

            publish.write_report_page(target, site_dir, "Manual run 123", "Manual", runs)

            html = (target / "index.html").read_text(encoding="utf-8")
            self.assertIn("YOLO video benchmark", html)
            self.assertIn("Median throughput", html)
            self.assertIn("10.500", html)
            self.assertIn("12.500", html)
            self.assertIn("PEK faster", html)
            self.assertNotIn("Dataset Analysis", html)
            self.assertNotIn('href="summary.json"', html)
            self.assertNotIn('href="summary.md"', html)
            self.assertNotIn('src="bare-detections.mp4"', html)
            self.assertNotIn('src="pek-detections.mp4"', html)

            (target / "summary.json").touch()
            (target / "summary.md").touch()
            (target / "bare-detections.mp4").touch()
            (target / "pek-detections.mp4").touch()
            publish.write_report_page(target, site_dir, "Manual run 123", "Manual", runs)
            html = (target / "index.html").read_text(encoding="utf-8")
            self.assertIn('href="summary.json"', html)
            self.assertIn('href="summary.md"', html)
            self.assertIn("Detection videos", html)
            self.assertIn("Bare / Ultralytics", html)
            self.assertIn('src="bare-detections.mp4"', html)
            self.assertIn('href="bare-detections.mp4"', html)
            self.assertIn('src="pek-detections.mp4"', html)
            self.assertIn('href="pek-detections.mp4"', html)

    def test_select_target_supports_manual_reports(self) -> None:
        with patch.dict(os.environ, {"UPSTREAM_RUN_ID": "123"}):
            target, pr_number, title = publish.select_target(Path("site"), "repo", "workflow_dispatch", "feature/test")

            self.assertEqual(target, Path("site") / "yolo-benchmark" / "manual" / "123")
            self.assertEqual(pr_number, "")
            self.assertEqual(title, "Manual run 123")

    def test_publish_report_skips_partial_artifact_without_comparisons(self) -> None:
        def write_partial_artifact(target: Path, *_args: object) -> bool:
            artifact = target / "yolo-benchmark"
            (artifact / "runs" / "run-01").mkdir(parents=True)
            (artifact / "images.tsv").write_text("", encoding="utf-8")
            return True

        env = {
            "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
            "UPSTREAM_EVENT": "workflow_dispatch",
            "UPSTREAM_HEAD_BRANCH": "feature/test",
            "UPSTREAM_HEAD_SHA": "a" * 40,
            "UPSTREAM_CONCLUSION": "failure",
            "UPSTREAM_RUN_ID": "123",
            "UPSTREAM_RUN_ATTEMPT": "1",
        }
        with tempfile.TemporaryDirectory() as tmpdir, patch.dict(os.environ, env), \
                patch.object(publish, "download_report_artifact", side_effect=write_partial_artifact), \
                patch.object(publish, "set_output") as set_output, \
                patch.object(publish, "checkout_site_branch") as checkout:
            publish.publish_report(Path(tmpdir), "pages")

            set_output.assert_called_once_with("deploy", "false")
            checkout.assert_not_called()

    def test_publish_report_keeps_detection_videos_out_of_storage_push(self) -> None:
        def write_artifact(destination: Path, *_args: object) -> bool:
            artifact = destination / "yolo-benchmark"
            run_dir = artifact / "runs" / "run-01"
            run_dir.mkdir(parents=True)
            (run_dir / "comparison.json").write_text(
                json.dumps(video_comparison()), encoding="utf-8"
            )
            (artifact / "bare-detections.mp4").write_bytes(b"bare-video")
            (artifact / "pek-detections.mp4").write_bytes(b"pek-video")
            return True

        def checkout(path: Path, _storage_branch: str) -> None:
            path.mkdir(parents=True)

        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "manual" / "123"

            def push(_site_dir: Path, _storage_branch: str) -> bool:
                self.assertFalse((target / "bare-detections.mp4").exists())
                self.assertFalse((target / "pek-detections.mp4").exists())
                return False

            env = {
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "workflow_dispatch",
                "UPSTREAM_HEAD_BRANCH": "feature/test",
                "UPSTREAM_HEAD_SHA": "a" * 40,
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
            }
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(publish, "download_report_artifact", side_effect=write_artifact), \
                    patch.object(publish, "checkout_site_branch", side_effect=checkout), \
                    patch.object(publish, "push_site_branch", side_effect=push), \
                    patch.object(publish, "set_output") as set_output:
                publish.publish_report(site_dir, "pages")

            self.assertEqual((target / "bare-detections.mp4").read_bytes(), b"bare-video")
            self.assertEqual((target / "pek-detections.mp4").read_bytes(), b"pek-video")
            self.assertIn(
                'src="bare-detections.mp4"',
                (target / "index.html").read_text(encoding="utf-8"),
            )
            self.assertIn(
                'src="pek-detections.mp4"',
                (target / "index.html").read_text(encoding="utf-8"),
            )
            set_output.assert_called_once_with("deploy", "true")

    def test_write_yolo_index_generates_index_for_existing_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            report_dir = site_dir / "yolo-benchmark" / "manual" / "123"
            run_dir = report_dir / "runs" / "run-01"
            run_dir.mkdir(parents=True, exist_ok=True)
            (run_dir / "comparison.json").write_text(json.dumps(comparison()), encoding="utf-8")
            (report_dir / "index.html").write_text("report", encoding="utf-8")
            (report_dir / "report-index-meta.txt").write_text("Manual | branch", encoding="utf-8")

            publish.write_yolo_index(site_dir, "Arm-Debug/amp-dev-forge")

            self.assertTrue((site_dir / "yolo-benchmark" / "index.html").is_file())

    def test_write_root_index_links_report_roots(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)

            publish.write_root_index(site_dir)

            parser = LinkParser()
            parser.feed((site_dir / "index.html").read_text(encoding="utf-8"))
            self.assertEqual(parser.hrefs, [
                "playwright/index.html",
                "yolo-benchmark/index.html",
                "yolo-performance-datasets/index.html",
            ])

    def test_restore_dataset_overlay_writes_deploy_only_dataset_page(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            image = root / "cache" / "coco" / "val2017" / "000000000139.jpg"
            image.parent.mkdir(parents=True)
            image.write_bytes(b"jpg")
            image_list = root / "images.tsv"
            image_list.write_text(
                "# image_set_fingerprint=sha256:abcdef1234567890\n139\t/tmp/000000000139.jpg\n",
                encoding="utf-8",
            )

            targets = overlay.restore_overlay(root / "site", root / "cache", image_list)
            target = targets[0]

            self.assertEqual(target.name, "coco-val2017-abcdef123456")
            self.assertTrue((target / "images" / "000000000139.jpg").is_file())
            self.assertTrue((target / "manifest.json").is_file())
            self.assertTrue((root / "site" / "yolo-performance-datasets" / "index.html").is_file())

    def test_restore_dataset_overlay_ignores_artifact_image_paths(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            cache_image = root / "cache" / "coco" / "val2017" / "000000000139.jpg"
            cache_image.parent.mkdir(parents=True)
            cache_image.write_bytes(b"expected")
            leaked = root / "secret.txt"
            leaked.write_bytes(b"secret")
            image_list = root / "images.tsv"
            image_list.write_text(
                f"# image_set_fingerprint=sha256:abcdef1234567890\n139\t{leaked}\n",
                encoding="utf-8",
            )

            target = overlay.restore_overlay(root / "site", root / "cache", image_list)[0]

            restored = target / "images" / "secret.txt"
            self.assertFalse(restored.exists())
            self.assertEqual((target / "images" / "000000000139.jpg").read_bytes(), b"expected")

    def test_restore_dataset_overlay_without_report_lists_only_writes_empty_index(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)

            with patch.object(overlay, "prepare_image_list", side_effect=AssertionError("unexpected download")):
                targets = overlay.restore_overlay(root / "site", root / "cache")

            self.assertEqual(targets, [])
            self.assertTrue((root / "site" / "yolo-performance-datasets" / "index.html").is_file())

    def test_restore_dataset_overlay_deduplicates_report_fingerprints(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            image = root / "cache" / "coco" / "val2017" / "000000000139.jpg"
            image.parent.mkdir(parents=True)
            image.write_bytes(b"jpg")
            for report in ("one", "two"):
                image_list = root / "site" / "yolo-benchmark" / report / "images.tsv"
                image_list.parent.mkdir(parents=True)
                image_list.write_text(
                    f"# image_set_fingerprint=sha256:abcdef1234567890\n139\t{image}\n",
                    encoding="utf-8",
                )

            targets = overlay.restore_overlay(root / "site", root / "cache")

            self.assertEqual([target.name for target in targets], ["coco-val2017-abcdef123456"])

    def test_remove_legacy_root_site_migrates_playwright_report_roots(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            (site_dir / "index.html").write_text("legacy", encoding="utf-8")
            (site_dir / "prs").mkdir()
            (site_dir / "playwright").mkdir()
            (site_dir / "yolo-benchmark").mkdir()

            self.assertTrue(publish.remove_legacy_root_site(site_dir))

            self.assertFalse((site_dir / "index.html").exists())
            self.assertFalse((site_dir / "prs").exists())
            self.assertTrue((site_dir / "playwright" / "prs").is_dir())
            self.assertTrue((site_dir / "playwright").is_dir())
            self.assertTrue((site_dir / "yolo-benchmark").is_dir())

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
                Path("bare") / "timings.jsonl",
            ):
                path = run_dir / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("data", encoding="utf-8")
            for name in (
                "summary.json", "summary.md", "video-source.json",
                "bare-detections.mp4", "pek-detections.mp4",
            ):
                (artifact / name).write_text("data", encoding="utf-8")

            target = root / "site"
            publish.write_selected_artifacts(artifact, target, [{"name": "run-01", "path": run_dir}])

            self.assertTrue((target / "runs" / "run-01" / "comparison.json").is_file())
            self.assertTrue((target / "summary.json").is_file())
            self.assertTrue((target / "video-source.json").is_file())
            self.assertTrue((target / "bare-detections.mp4").is_file())
            self.assertTrue((target / "pek-detections.mp4").is_file())
            self.assertFalse((target / "runs" / "run-01" / "bare" / "predictions.jsonl").exists())
            self.assertFalse((target / "runs" / "run-01" / "bare" / "timings.jsonl").exists())

    def test_local_artifact_copy_ignores_cache_dirs(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            ignore = publish.local_artifact_ignore(root)

            ignored = ignore(str(root), ["images.tsv", "runs", ".venv", "pek-build", "comparison.json"])

            self.assertEqual(ignored, {".venv", "pek-build", "comparison.json"})
            self.assertEqual(
                ignore(str(root), [
                    "video-source.json", "summary.json", "summary.md",
                    "bare-detections.mp4", "pek-detections.mp4",
                ]),
                set(),
            )
            self.assertEqual(ignore(str(root / "runs" / "run-01"),
                                    ["predictions.jsonl", "timings.jsonl", "comparison.json"]),
                             {"predictions.jsonl"})


if __name__ == "__main__":
    unittest.main()
