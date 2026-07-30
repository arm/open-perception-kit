#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import hashlib
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
from scripts.report_pages import publish as report_pages  # noqa: E402
VIDEO_SHA256 = "a" * 64


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
        "schema": publish.IMAGE_COMPARISON_SCHEMA,
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
        "schema": "expkits_yolo_video_comparison.v3",
        "measurement": {
            "technique": "preloaded_video_result_intervals",
            "timed_region": "first_serialized_result_ready_to_last_serialized_result_ready",
            "decode_included": False,
            "source_color_conversion_included": False,
            "preloaded_frames": True,
            "artifact_write_excluded": True,
            "video_pacing_disabled": True,
            "warmup_video_passes": 1,
            "warmup_frames": 1,
        },
        "inputs": {
            "source_frame_count": 205,
            "source_width": 1920,
            "source_height": 1080,
            "source_fps": 30.0,
            "video_sha256": VIDEO_SHA256,
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
            target = site_dir / "yolo-imageset-benchmark" / "manual" / "123"
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
            (target / "video-source.json").write_text(
                json.dumps({
                    "schema": "expkits_yolo_video_source.v1",
                    "sha256": VIDEO_SHA256,
                }),
                encoding="utf-8",
            )
            runs = [
                {"name": "run-01", "path": Path("run-01"), "comparison": video_comparison(10.0, 12.0)},
                {"name": "run-02", "path": Path("run-02"), "comparison": video_comparison(11.0, 13.0)},
            ]

            run_href = "https://github.com/Arm-Debug/amp-dev-forge/actions/runs/123"
            publish.write_report_page(target, site_dir, "Manual run 123", "Manual", runs, run_href)

            html = (target / "index.html").read_text(encoding="utf-8")
            self.assertIn("YOLO video benchmark", html)
            self.assertIn("Median throughput", html)
            self.assertIn("10.500", html)
            self.assertIn("12.500", html)
            self.assertIn("PEK faster", html)
            input_href = (
                "../../../yolo-performance-datasets/"
                f"mediapipe-object-detection-{VIDEO_SHA256[:12]}/index.html"
            )
            self.assertIn(f'href="{input_href}"', html)
            self.assertNotIn("Dataset Analysis", html)
            self.assertNotIn('href="summary.json"', html)
            self.assertNotIn('href="summary.md"', html)
            self.assertNotIn('src="bare-detections.mp4"', html)
            self.assertNotIn('src="pek-detections.mp4"', html)
            self.assertIn(f'<a href="{run_href}">Open workflow run</a>', html)

            (target / "summary.json").touch()
            (target / "summary.md").touch()
            (target / "bare-detections.mp4").touch()
            (target / "pek-detections.mp4").touch()
            publish.write_report_page(target, site_dir, "Manual run 123", "Manual", runs, run_href)
            html = (target / "index.html").read_text(encoding="utf-8")
            self.assertIn('href="summary.json"', html)
            self.assertIn('href="summary.md"', html)
            self.assertIn("Detection videos", html)
            self.assertIn("Bare / Ultralytics", html)
            self.assertIn('src="bare-detections.mp4"', html)
            self.assertIn('href="bare-detections.mp4"', html)
            self.assertIn('src="pek-detections.mp4"', html)
            self.assertIn('href="pek-detections.mp4"', html)

    def test_write_video_report_rejects_manifest_sha_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "manual" / "123"
            target.mkdir(parents=True)
            (target / "video-source.json").write_text("[]", encoding="utf-8")

            with self.assertRaisesRegex(publish.PublishError, "must use"):
                publish.write_report_page(
                    target,
                    site_dir,
                    "Manual run 123",
                    "Manual",
                    [{"name": "run-01", "path": Path("run-01"), "comparison": video_comparison()}],
                )

            (target / "video-source.json").write_text(
                json.dumps({
                    "schema": "expkits_yolo_video_source.v1",
                    "sha256": "b" * 64,
                }),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(publish.PublishError, "differs"):
                publish.write_report_page(
                    target,
                    site_dir,
                    "Manual run 123",
                    "Manual",
                    [{"name": "run-01", "path": Path("run-01"), "comparison": video_comparison()}],
                )

    def test_select_target_supports_manual_reports(self) -> None:
        with patch.dict(os.environ, {"UPSTREAM_RUN_ID": "123"}):
            target, pr_number, title = publish.select_target(Path("site"), "repo", "workflow_dispatch", "feature/test")

            self.assertEqual(target, Path("site") / "yolo-benchmark" / "manual" / "123")
            self.assertEqual(pr_number, "")
            self.assertEqual(title, "Manual run 123")

    def test_imageset_workflow_targets_separate_report_root(self) -> None:
        with patch.dict(os.environ, {"UPSTREAM_RUN_ID": "123"}):
            report_root = publish.report_root_for_runs([{"comparison": comparison()}])
            target, _, _ = publish.select_target(
                Path("site"), "repo", "workflow_dispatch", "feature/test", report_root
            )

        self.assertEqual(target, Path("site") / "yolo-imageset-benchmark" / "manual" / "123")
        self.assertEqual(
            publish.report_root_for_runs([{"comparison": video_comparison()}]),
            "yolo-benchmark",
        )
        with self.assertRaisesRegex(publish.PublishError, "unsupported comparison schema"):
            publish.report_root_for_runs([{"comparison": {"schema": "unknown", "timing_delta": {}}}])

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
            checkout.assert_called_once()

    def test_publish_report_keeps_video_and_imageset_nightlies_separate(self) -> None:
        def write_artifact(destination: Path, _repository: str, run_id: str, _attempt: str) -> bool:
            artifact = destination / "yolo-benchmark"
            run_dir = artifact / "runs" / "run-01"
            run_dir.mkdir(parents=True)
            report = comparison() if run_id == "200" else video_comparison()
            (run_dir / "comparison.json").write_text(json.dumps(report), encoding="utf-8")
            if run_id != "200":
                (artifact / "video-source.json").write_text(
                    json.dumps({"schema": "expkits_yolo_video_source.v1", "sha256": VIDEO_SHA256}),
                    encoding="utf-8",
                )
            return True

        base_env = {
            "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
            "UPSTREAM_CONCLUSION": "success",
            "UPSTREAM_EVENT": "schedule",
            "UPSTREAM_HEAD_BRANCH": "develop",
            "UPSTREAM_HEAD_SHA": "a" * 40,
            "UPSTREAM_RUN_ATTEMPT": "1",
        }
        with tempfile.TemporaryDirectory() as tmpdir, \
                patch.object(publish, "checkout_site_branch", side_effect=lambda path, _: path.mkdir(exist_ok=True)), \
                patch.object(publish, "download_report_artifact", side_effect=write_artifact), \
                patch.object(publish, "push_site_branch", return_value=True), \
                patch.object(publish, "set_output"):
            site_dir = Path(tmpdir) / "site"
            with patch.dict(os.environ, {**base_env, "UPSTREAM_RUN_ID": "200"}, clear=True):
                publish.publish_report(site_dir, "pages")
            with patch.dict(os.environ, {**base_env, "UPSTREAM_RUN_ID": "100"}, clear=True):
                publish.publish_report(site_dir, "pages")

            self.assertTrue((site_dir / "yolo-imageset-benchmark" / "nightly" / "index.html").is_file())
            self.assertTrue((site_dir / "yolo-benchmark" / "nightly" / "index.html").is_file())
            self.assertIn("run 200", (site_dir / "yolo-imageset-benchmark" / "nightly" /
                                      publish.REPORT_INDEX_META).read_text(encoding="utf-8"))
            self.assertIn("run 100", (site_dir / "yolo-benchmark" / "nightly" /
                                      publish.REPORT_INDEX_META).read_text(encoding="utf-8"))

    def test_publish_report_skips_stale_attempt_after_artifact_classification(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "prs" / "235"

            def write_artifact(destination: Path, *_args: object) -> bool:
                run_dir = destination / "yolo-benchmark" / "runs" / "run-01"
                run_dir.mkdir(parents=True)
                (run_dir / "comparison.json").write_text(json.dumps(video_comparison()), encoding="utf-8")
                return True

            def checkout(_path: Path, _storage_branch: str) -> None:
                target.mkdir(parents=True)
                (target / "marker.txt").write_text("newer", encoding="utf-8")
                (target / publish.REPORT_INDEX_META).write_text(
                    "branch @ commit | run 123 attempt 2 | Jul 24, 2026 10:00 UTC\n",
                    encoding="utf-8",
                )

            env = {
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "pull_request",
                "UPSTREAM_HEAD_BRANCH": "feature/test",
                "UPSTREAM_HEAD_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_HEAD_SHA": "a" * 40,
                "UPSTREAM_PR_NUMBER": "235",
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
            }
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(publish, "checkout_site_branch", side_effect=checkout), \
                    patch.object(publish, "download_report_artifact", side_effect=write_artifact) as download, \
                    patch.object(publish, "push_site_branch") as push, \
                    patch.object(publish, "set_output") as set_output:
                publish.publish_report(site_dir, "pages")

            download.assert_called_once()
            push.assert_not_called()
            self.assertEqual((target / "marker.txt").read_text(encoding="utf-8"), "newer")
            set_output.assert_called_once_with("deploy", "false")

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
                html = (target / "index.html").read_text(encoding="utf-8")
                self.assertNotIn('src="bare-detections.mp4"', html)
                self.assertNotIn('src="pek-detections.mp4"', html)
                self.assertIn(
                    '<a href="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/123">Open workflow run</a>',
                    html,
                )
                manifest = json.loads((target / publish.VIDEO_ARTIFACT_META).read_text(encoding="utf-8"))
                self.assertEqual(manifest["files"], list(publish.DETECTION_VIDEOS))
                return True

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

            self.assertFalse((target / "bare-detections.mp4").exists())
            self.assertFalse((target / "pek-detections.mp4").exists())
            self.assertIn(">Open workflow run</a>", (target / "index.html").read_text(encoding="utf-8"))
            set_output.assert_called_once_with("deploy", "true")

    def test_restore_latest_detection_videos_preserves_latest_v2_report(self) -> None:
        repository = "Arm-Debug/amp-dev-forge"
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"

            def write_report(run_id: str, comparison_doc: dict | None = None) -> Path:
                target = site_dir / "yolo-benchmark" / "manual" / run_id
                run_dir = target / "runs" / "run-01"
                run_dir.mkdir(parents=True)
                (run_dir / "comparison.json").write_text(
                    json.dumps(comparison_doc or video_comparison()), encoding="utf-8"
                )
                (target / "report-meta.html").write_text("Manual\n", encoding="utf-8")
                publish.write_video_artifact_meta(
                    target, repository, run_id, "1", f"Manual run {run_id}", list(publish.DETECTION_VIDEOS)
                )
                publish.write_report_page(
                    target,
                    site_dir,
                    f"Manual run {run_id}",
                    "Manual",
                    publish.load_report_runs(target),
                    f"https://github.com/{repository}/actions/runs/{run_id}",
                )
                return target

            old_target = write_report("123")
            v2_comparison = video_comparison()
            v2_comparison["schema"] = "expkits_yolo_video_comparison.v2"
            v2_comparison["measurement"] = {
                "technique": "unpaced_video_result_intervals",
                "timed_region": "first_serialized_result_ready_to_last_serialized_result_ready",
                "decode_included": True,
                "artifact_write_excluded": True,
                "video_pacing_disabled": True,
                "warmup_video_passes": 1,
                "warmup_frames": 1,
            }
            latest_target = write_report("456", v2_comparison)

            def write_artifact(destination: Path, _repository: str, run_id: str, _attempt: str) -> bool:
                self.assertEqual(run_id, "456")
                artifact = destination / "yolo-benchmark"
                (artifact / "runs").mkdir(parents=True)
                (artifact / "bare-detections.mp4").write_bytes(b"bare-video")
                (artifact / "pek-detections.mp4").write_bytes(b"pek-video")
                return True

            with patch.dict(os.environ, {"GITHUB_REPOSITORY": repository}), \
                    patch.object(publish, "download_report_artifact", side_effect=write_artifact):
                self.assertTrue(publish.restore_latest_detection_videos(site_dir))

            self.assertNotIn('src="bare-detections.mp4"', (old_target / "index.html").read_text(encoding="utf-8"))
            latest_html = (latest_target / "index.html").read_text(encoding="utf-8")
            self.assertIn('src="bare-detections.mp4"', latest_html)
            self.assertIn('src="pek-detections.mp4"', latest_html)
            self.assertIn("Unpaced pipeline", latest_html)
            self.assertIn("FPS includes decode, color conversion", latest_html)
            self.assertNotIn("Preloaded video stream", latest_html)

    def test_restore_latest_detection_videos_links_when_artifact_is_missing(self) -> None:
        repository = "Arm-Debug/amp-dev-forge"
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            target = site_dir / "yolo-benchmark" / "manual" / "123"
            target.mkdir(parents=True)
            publish.write_video_artifact_meta(
                target, repository, "123", "1", "Manual run 123", list(publish.DETECTION_VIDEOS)
            )

            with patch.dict(os.environ, {"GITHUB_REPOSITORY": repository}), \
                    patch.object(publish, "download_report_artifact", return_value=False):
                self.assertFalse(publish.restore_latest_detection_videos(site_dir))

    def test_write_yolo_index_generates_index_for_existing_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            report_dir = site_dir / "yolo-imageset-benchmark" / "manual" / "123"
            run_dir = report_dir / "runs" / "run-01"
            run_dir.mkdir(parents=True, exist_ok=True)
            (run_dir / "comparison.json").write_text(json.dumps(comparison()), encoding="utf-8")
            (report_dir / "index.html").write_text("report", encoding="utf-8")
            (report_dir / "report-index-meta.txt").write_text("Manual | branch", encoding="utf-8")

            publish.write_index_assets(site_dir)
            publish.write_yolo_index(
                site_dir,
                "Arm-Debug/amp-dev-forge",
                publish.IMAGESET_REPORT_ROOT,
                publish.IMAGESET_PRODUCT_TITLE,
            )

            self.assertTrue((site_dir / "yolo-imageset-benchmark" / "index.html").is_file())
            for report_root in publish.REPORT_ROOTS:
                self.assertTrue((site_dir / report_root / "report-index.css").is_file())

    def test_cleanup_closed_pr_reports_removes_both_report_roots(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            for report_root in publish.REPORT_ROOTS:
                (site_dir / report_root / "prs" / "123").mkdir(parents=True)

            with patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}), \
                    patch.object(publish, "checkout_site_branch"), \
                    patch.object(publish, "pr_state", return_value={"state": "MERGED", "closedAt": "2020-01-01T00:00:00Z"}), \
                    patch.object(publish, "push_site_branch", return_value=True), \
                    patch.object(publish, "set_output") as set_output:
                publish.cleanup_closed_pr_reports(site_dir, "pages", retention_days=0)

            for report_root in publish.REPORT_ROOTS:
                self.assertFalse((site_dir / report_root / "prs" / "123").exists())
            set_output.assert_called_once_with("deploy", "true")

    def test_write_yolo_index_routes_persisted_video_report_by_shape(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            report_dir = site_dir / "yolo-benchmark" / "manual" / "123"
            run_dir = report_dir / "runs" / "run-01"
            run_dir.mkdir(parents=True, exist_ok=True)
            persisted = video_comparison()
            persisted["schema"] = "expkits_yolo_video_comparison.v1"
            (run_dir / "comparison.json").write_text(
                json.dumps(persisted),
                encoding="utf-8",
            )
            (report_dir / "index.html").write_text("report", encoding="utf-8")
            (report_dir / "report-index-meta.txt").write_text(
                "Manual | branch",
                encoding="utf-8",
            )

            publish.write_yolo_index(site_dir, "Arm-Debug/amp-dev-forge")

            index = (site_dir / "yolo-benchmark" / "index.html").read_text(
                encoding="utf-8"
            )
            self.assertIn("PEK faster by 20.0%", index)

    def test_write_root_index_links_report_roots(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            nightly = site_dir / "yolo-benchmark" / "nightly"
            nightly.mkdir(parents=True)
            publish.write_report_page(
                nightly,
                site_dir,
                "Latest nightly",
                "Nightly",
                [{"name": "run-01", "path": nightly / "runs" / "run-01",
                  "comparison": video_comparison(bare_fps=10.0, pek_fps=12.0)}],
            )

            self.assertEqual(
                report_pages.yolo_nightly_badge(site_dir, "yolo-benchmark"),
                ("fast", "PEK faster by 20.0%"),
            )
            self.assertEqual(
                report_pages.root_card_badge(
                    site_dir,
                    "yolo-performance-datasets/index.html",
                    1,
                ),
                ("fast", "1 input"),
            )
            publish.write_root_index(site_dir)

            parser = LinkParser()
            parser.feed((site_dir / "index.html").read_text(encoding="utf-8"))
            self.assertEqual(parser.hrefs, [
                "yolo-benchmark/index.html",
                "yolo-imageset-benchmark/index.html",
                "playwright/index.html",
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

            with patch.object(overlay, "write_root_index") as write_root_index:
                targets = overlay.restore_overlay(root / "site", root / "cache", image_list)
            target = targets[0]

            self.assertEqual(target.name, "coco-val2017-abcdef123456")
            self.assertTrue((target / "images" / "000000000139.jpg").is_file())
            self.assertTrue((target / "manifest.json").is_file())
            self.assertTrue((root / "site" / "yolo-performance-datasets" / "index.html").is_file())
            write_root_index.assert_called_once_with(root / "site", dataset_count=1)

    def test_restore_dataset_overlay_writes_input_video_preview(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            video = b"pinned-video"
            digest = hashlib.sha256(video).hexdigest()
            manifest = root / "site" / "yolo-benchmark" / "manual" / "123" / "video-source.json"
            manifest.parent.mkdir(parents=True)
            source = {
                "schema": "expkits_yolo_video_source.v1",
                "name": "MediaPipe object detection test video",
                "repository": "https://github.com/google-ai-edge/mediapipe",
                "revision": "test-revision",
                "url": "https://example.test/video.mp4",
                "license": "Apache-2.0",
                "sha256": digest,
                "video": "/cache/video.mp4",
                "width": 1920,
                "height": 1080,
                "fps": 30.0,
                "frame_count": 205,
            }
            manifest.write_text(json.dumps(source), encoding="utf-8")
            cached_video = root / "prepared-video.mp4"
            cached_video.write_bytes(video)

            with patch.object(overlay, "prepare_video_source", return_value=(cached_video, dict(source))):
                targets = overlay.restore_overlay(root / "site", root / "cache")

            target = targets[0]
            self.assertEqual(target.name, f"mediapipe-object-detection-{digest[:12]}")
            self.assertEqual((target / overlay.VIDEO_FILENAME).read_bytes(), video)
            self.assertEqual(json.loads((target / "manifest.json").read_text())["video"], overlay.VIDEO_FILENAME)
            html = (target / "index.html").read_text(encoding="utf-8")
            self.assertIn(f'<video controls preload="metadata" playsinline src="{overlay.VIDEO_FILENAME}">', html)

    def test_restore_dataset_overlay_skips_bad_manifest_before_valid_one(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            video = b"pinned-video"
            digest = hashlib.sha256(video).hexdigest()
            canonical = {
                "schema": "expkits_yolo_video_source.v1",
                "name": "MediaPipe object detection test video",
                "repository": "https://github.com/google-ai-edge/mediapipe",
                "revision": "test-revision",
                "url": "https://example.test/video.mp4",
                "license": "Apache-2.0",
                "sha256": digest,
                "width": 1920,
                "height": 1080,
                "fps": 30.0,
                "frame_count": 205,
            }
            bad = root / "site" / "yolo-benchmark" / "a" / "video-source.json"
            good = root / "site" / "yolo-benchmark" / "b" / "video-source.json"
            bad.parent.mkdir(parents=True)
            good.parent.mkdir(parents=True)
            bad.write_text(json.dumps(dict(canonical, schema="unsupported")), encoding="utf-8")
            good.write_text(json.dumps(canonical), encoding="utf-8")
            cached_video = root / "prepared-video.mp4"
            cached_video.write_bytes(video)

            with patch.object(overlay, "prepare_video_source", return_value=(cached_video, canonical)):
                targets = overlay.restore_overlay(root / "site", root / "cache")

            self.assertEqual([target.name for target in targets], [
                f"mediapipe-object-detection-{digest[:12]}"
            ])

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

    def test_restore_dataset_overlay_discovers_imageset_report_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            image = root / "cache" / "coco" / "val2017" / "000000000139.jpg"
            image.parent.mkdir(parents=True)
            image.write_bytes(b"jpg")
            image_list = root / "site" / "yolo-imageset-benchmark" / "nightly" / "images.tsv"
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
