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
        "inputs": {
            "image_count": 2,
            "imgsz": 320,
            "device": "cpu",
            "bare_model": "config/models/yolov11/yolo11n-fp32-320.onnx",
            "pek_opchain": "config/models/yolov11/opchain.json",
        },
        "timing_delta": {"per_image_ms": {metric: dict(row) for metric in publish.RUN_METRICS}},
    }


class TestPublishYoloBenchmarkPages(unittest.TestCase):
    def test_benchmark_tables_use_uniform_layout(self) -> None:
        css = SCRIPT_PATH.with_name("assets").joinpath("report-index.css").read_text(encoding="utf-8")

        self.assertIn("table-layout: fixed;", css)
        self.assertIn("display: grid;", css)
        self.assertIn("grid-template-columns: minmax(0, 5fr) minmax(0, 6fr);", css)
        self.assertIn("width: 19%;", css)
        self.assertIn("width: 32%;", css)
        self.assertIn("width: 100%;", css)
        self.assertIn("scrollbar-gutter: stable both-edges;", css)
        self.assertIn("--accent: #1a7f37;", css)
        self.assertIn("--bare: #57606a;", css)
        self.assertIn("border-bottom: 2px solid var(--accent);", css)
        self.assertNotIn("--accent: #0969da;", css)
        self.assertNotIn(".run-table-panel", css)
        self.assertIn(".summary-heading", css)
        self.assertIn(".report-overall", css)
        self.assertIn(".report-link > .verdict", css)
        self.assertIn(".metric-tab-controls", css)
        self.assertIn(".metric-tab-panel {\n  display: none;", css)
        self.assertIn(".run-tabs", css)
        self.assertIn(".run-tab-label.is-active", css)
        self.assertIn(".run-comparison-link", css)
        self.assertIn(".run-card", css)
        self.assertNotIn(".run-panel-heading", css)
        self.assertNotIn(".run-section", css)
        self.assertNotIn(".benchmark-table th span", css)
        self.assertIn(".chart-bare {\n  fill: var(--bare);\n  stroke: var(--bare);", css)
        self.assertIn(".chart-pek {\n  fill: var(--pass);\n  stroke: var(--pass);", css)
        self.assertIn(".chart-line.chart-bare,\n.chart-line.chart-pek {\n  fill: none;", css)
        self.assertNotIn(".metric-guide", css)
        self.assertIn(".chart-value-label", css)
        self.assertIn(".chart-svg-title", css)
        self.assertIn(".chart-svg-legend text", css)
        self.assertIn("font: 15px", css)
        self.assertIn("font-size: 20px;", css)
        self.assertIn("font-size: 25px;", css)
        self.assertNotIn("font-size: 11px;", css)
        self.assertNotIn(".chart-title-row", css)
        self.assertNotIn(".chart-legend span", css)
        self.assertIn(".chart-hit-area", css)
        self.assertIn(".chart-axis-label", css)
        self.assertIn(".chart-y-tick", css)
        self.assertIn(".chart-tooltip", css)
        self.assertIn(".report-header-row", css)
        self.assertIn(".top-back-link", css)
        self.assertIn(".section-card", css)
        self.assertIn(".section-heading", css)
        self.assertIn(".section-toggle[aria-expanded=\"true\"]::before", css)
        self.assertIn(".info-grid", css)
        self.assertNotIn(".benchmark-block", css)

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
            self.assertTrue(content.startswith("<!DOCTYPE html>"))
            self.assertIn('<html lang="en">', content)
            self.assertNotIn('style="scrollbar-gutter:', content)
            self.assertIn("10-run Summary", content)
            self.assertEqual(content.count('href="../../index.html"'), 2)
            self.assertIn('<div class="report-header-row"><div>', content)
            self.assertIn('<a class="back-link top-back-link" href="../../index.html">Back to YOLO report index</a>',
                          content)
            self.assertIn('<div class="report-overall"><span>Overall</span>'
                          '<span class="verdict verdict-slow">PEK slower by 57.1%</span></div>', content)
            self.assertLess(content.index("report-overall"), content.index("10-run Summary"))
            self.assertIn('<h2>10-run Summary</h2>', content)
            self.assertIn(
                '<div class="section-heading"><button class="section-toggle" type="button" '
                'aria-expanded="false" aria-controls="section-information"><span>Information</span></button></div>',
                content,
            )
            self.assertIn('<div id="section-information" hidden>', content)
            self.assertIn("Measurement Cut", content)
            self.assertIn("Images are preloaded in memory before the timed loop", content)
            self.assertIn("Model/OpChain load, image file I/O, JPEG decode, preload", content)
            self.assertIn("Bare Run", content)
            self.assertIn("Python Ultralytics YOLO predict loop", content)
            self.assertIn("PEK Run", content)
            self.assertIn("C++ PEK OpChain runner", content)
            self.assertIn("OpChain Operations", content)
            self.assertIn("pek-std-ops/GenericImagePreprocess", content)
            self.assertIn("pek-onnx-ops/Inference", content)
            self.assertIn("parser=YoloParser", content)
            self.assertIn("Metrics &amp; Stability", content)
            self.assertIn("<code>avg_ms</code>: average per-image time", content)
            self.assertIn("<code>p50_ms</code>: median per-image time", content)
            self.assertIn("95% of images were this fast or faster", content)
            self.assertIn("99% of images were this fast or faster", content)
            self.assertIn("benchmark stability across runs", content)
            self.assertIn("lower and flatter is steadier", content)
            self.assertLess(content.index("Measurement Cut"), content.index("Metrics &amp; Stability"))
            self.assertLess(content.index("Metrics &amp; Stability"), content.index("Bare Run"))
            self.assertLess(content.index("Bare Run"), content.index("PEK Run"))
            self.assertLess(content.index("Information"), content.index("10-run Summary"))
            self.assertNotIn('<section class="section-card run-section">', content)
            self.assertNotIn('<details class="benchmark-block"', content)
            self.assertNotIn("Benchmark Definition", content)
            self.assertIn(
                "<p>preloaded_image_to_postprocess_result_ready | 2 images | imgsz 320 | cpu | 2 runs</p>",
                content,
            )
            self.assertLess(
                content.index("Manual"),
                content.index("preloaded_image_to_postprocess_result_ready"),
            )
            self.assertLess(
                content.index("preloaded_image_to_postprocess_result_ready"),
                content.index("10-run Summary"),
            )
            self.assertNotIn('<details class="metric-guide">', content)
            self.assertNotIn("Metric guide", content)
            self.assertIn("median per-image time", content)
            self.assertIn("75% of images were this fast or faster", content)
            self.assertIn("95% of images were this fast or faster", content)
            self.assertIn("99% of images were this fast or faster", content)
            self.assertNotIn("Median metric comparison", content)
            self.assertIn('<div class="summary-heading"><h2>10-run Summary</h2>', content)
            self.assertNotIn("summary-overall", content)
            self.assertIn('<label class="metric-tab-label is-active"><input class="metric-tab-input" type="radio" '
                          'name="summary-metric" id="summary-metric-p50-ms" '
                          'aria-controls="summary-metric-p50-ms-panel" checked>p50_ms</label>',
                          content)
            self.assertIn('<label class="metric-tab-label"><input class="metric-tab-input" type="radio" '
                          'name="summary-metric" id="summary-metric-p75-ms" '
                          'aria-controls="summary-metric-p75-ms-panel">p75_ms</label>',
                          content)
            self.assertIn('<label class="metric-tab-label"><input class="metric-tab-input" type="radio" '
                          'name="summary-metric" id="summary-metric-p99-ms" '
                          'aria-controls="summary-metric-p99-ms-panel">p99_ms</label>',
                          content)
            self.assertIn('<div class="metric-tab-controls">', content)
            self.assertIn('class="metric-tab-panel is-active" id="summary-metric-p50-ms-panel"', content)
            self.assertIn('class="metric-tab-panel summary-detail-panel is-active" '
                          'id="summary-metric-p50-ms-details" data-metric-panel="summary-metric-p50-ms-panel"',
                          content)
            self.assertIn('class="metric-tab-panel summary-detail-panel" id="summary-metric-p99-ms-details" '
                          'data-metric-panel="summary-metric-p99-ms-panel"', content)
            self.assertLess(
                content.index('<div class="metric-tab-controls">'),
                content.index('<div class="metric-tab-panels">'),
            )
            self.assertNotIn("avg_ms trend", content)
            self.assertIn("p50_ms trend", content)
            self.assertIn("p75_ms trend", content)
            self.assertIn("p99_ms trend", content)
            self.assertIn(
                '<text class="chart-svg-title" x="18" y="38">p50_ms trend</text>'
                '<g class="chart-svg-legend"',
                content,
            )
            self.assertIn('<text x="12" y="5">Bare</text>', content)
            self.assertIn('<text x="84" y="5">PEK</text>', content)
            self.assertNotIn('<div class="chart-title-row"', content)
            self.assertIn("p50_ms trend across runs", content)
            self.assertIn("class=\"metric-chart line-chart\"", content)
            self.assertIn("class=\"metric-chart bar-chart\"", content)
            self.assertIn('viewBox="0 0 960 540"', content)
            self.assertIn('<text class="chart-axis-label chart-x-axis-label"', content)
            self.assertIn('<text class="chart-y-tick"', content)
            self.assertIn('<text class="chart-y-tick" x="150"', content)
            self.assertIn(">Run</text>", content)
            self.assertIn(">Metric</text>", content)
            self.assertIn(">Time [ms]</text>", content)
            self.assertIn('y="54">Time [ms]</text>', content)
            self.assertIn('data-tooltip="Bare p50_ms 01: 10.000 ms"', content)
            self.assertIn('data-tooltip="PEK p50_ms 01: 15.000 ms"', content)
            self.assertIn('data-tooltip="Bare avg_ms: 10.000 ms"', content)
            self.assertIn('data-tooltip="PEK avg_ms: 15.000 ms"', content)
            self.assertIn('class="chart-hit-area"', content)
            self.assertIn('<div class="chart-tooltip" role="tooltip" hidden></div>', content)
            self.assertIn('document.addEventListener("pointerover"', content)
            self.assertIn('document.addEventListener("click"', content)
            self.assertIn('<text class="chart-svg-title" x="18" y="38">Metric comparison</text>', content)
            self.assertIn("<rect class=\"chart-bar chart-bare\"", content)
            self.assertIn("<rect class=\"chart-bar chart-pek\"", content)
            self.assertIn("<path class=\"chart-line chart-bare\"", content)
            self.assertIn("chart-value-label", content)
            self.assertIn('data-tooltip="Bare avg_ms: 10.000 ms" '
                          'aria-label="Bare avg_ms: 10.000 ms">10.0</text>', content)
            self.assertIn('<div class="benchmark-layout"><div class="chart-panel">', content)
            self.assertIn('</div><div class="table-panel"><div class="table-scroll">', content)
            self.assertNotIn("run-table-panel", content)
            self.assertLess(
                content.index('<text class="chart-svg-title" x="18" y="38">p50_ms trend</text>'),
                content.index('<g class="chart-grid">'),
            )
            self.assertNotIn("<polyline", content)
            self.assertIn("run-01", content)
            self.assertIn("50.0%", content)
            self.assertIn("PEK slower by 50.0%", content)
            self.assertIn("PEK slower by 63.6%", content)
            self.assertNotIn(">0.000</text>", content)
            self.assertIn('<th scope="col">Bare med/sd [ms]</th>', content)
            self.assertIn('<th scope="col">PEK med/sd [ms]</th>', content)
            self.assertIn("10.500 / 0.707", content)
            self.assertIn("16.500 / 2.121", content)
            self.assertIn('<th scope="col">Bare [ms]</th>', content)
            self.assertNotIn("<th scope=\"col\"><span>", content)
            self.assertNotIn("<th>ratio</th>", content)
            self.assertNotIn("<th>delta %</th>", content)
            self.assertIn('<h2>10-run Summary</h2>', content)
            self.assertIn('<h2>Runs</h2>', content)
            self.assertNotIn('aria-controls="section-summary"', content)
            self.assertNotIn('aria-controls="section-runs"', content)
            self.assertIn('<label class="run-tab-label is-active"><input class="run-tab-input" type="radio" '
                          'name="run-tab" id="run-tab-1" aria-controls="run-tab-panel-1" '
                          'data-comparison-href="runs/run-01/comparison.json" checked>run-01</label>',
                          content)
            self.assertIn('<label class="run-tab-label"><input class="run-tab-input" type="radio" '
                          'name="run-tab" id="run-tab-2" aria-controls="run-tab-panel-2" '
                          'data-comparison-href="runs/run-02/comparison.json">run-02</label>',
                          content)
            self.assertNotIn("Keyboard: Left/Right switches runs.", content)
            self.assertIn('<a class="title-link run-comparison-link" href="runs/run-01/comparison.json">'
                          'comparison.json</a>', content)
            self.assertNotIn("run-panel-heading", content)
            self.assertNotIn("Raw Artifacts", content)
            self.assertIn('href="../../report-index.css"', content)

    def test_select_target_supports_manual_reports(self) -> None:
        with patch.dict(os.environ, {"UPSTREAM_RUN_ID": "123"}):
            target, pr_number, title = publish.select_target(Path("site"), "repo", "workflow_dispatch", "feature/test")

            self.assertEqual(target, Path("site") / "yolo-benchmark" / "manual" / "123")
            self.assertEqual(pr_number, "")
            self.assertEqual(title, "Manual run 123")

    def test_yolo_index_uses_overall_result_badge(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            report_dir = site_dir / "yolo-benchmark" / "manual" / "123"
            for index, run in enumerate((comparison(), comparison(11.0, 18.0)), start=1):
                run_dir = report_dir / "runs" / f"run-{index:02d}"
                run_dir.mkdir(parents=True, exist_ok=True)
                (run_dir / "comparison.json").write_text(json.dumps(run), encoding="utf-8")
            (report_dir / "index.html").write_text("report", encoding="utf-8")
            (report_dir / "report-index-meta.txt").write_text("Manual | branch", encoding="utf-8")

            publish.write_yolo_index(site_dir, "Arm-Debug/amp-dev-forge")

            content = (site_dir / "yolo-benchmark" / "index.html").read_text(encoding="utf-8")
            self.assertIn("Run 123", content)
            self.assertIn('<span class="verdict verdict-slow">PEK slower by 57.1%</span>', content)
            self.assertNotIn(">Open<", content)

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
