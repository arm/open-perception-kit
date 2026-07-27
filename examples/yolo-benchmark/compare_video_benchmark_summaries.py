#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Compare or aggregate Bare and PEK video benchmark summaries."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Any


SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "video_benchmark_summary.schema.json"
SUMMARY_SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
SUMMARY_SCHEMA_ID = SUMMARY_SCHEMA["properties"]["schema"]["const"]
MEASUREMENT_CONSTS = {
    key: value["const"]
    for key, value in SUMMARY_SCHEMA["properties"]["measurement"]["properties"].items()
    if "const" in value
}
COMPARISON_SCHEMA = "expkits_yolo_video_comparison.v2"
REPORT_SCHEMA = "expkits_yolo_video_report.v2"
COMPARABLE_INPUTS = (
    "video_sha256",
    "source_width",
    "source_height",
    "source_fps",
    "source_frame_count",
    "imgsz",
    "device",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bare-summary", type=Path)
    parser.add_argument("--pek-summary", type=Path)
    parser.add_argument("--runs-root", type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def fps_delta(bare_fps: float, pek_fps: float) -> dict[str, float]:
    if bare_fps <= 0 or pek_fps <= 0:
        raise ValueError("pipeline FPS values must be positive")
    ratio = pek_fps / bare_fps
    return {
        "bare_fps": bare_fps,
        "pek_fps": pek_fps,
        "delta_fps": pek_fps - bare_fps,
        "ratio": ratio,
        "delta_percent": (ratio - 1.0) * 100.0,
    }


def validate_summary(doc: dict[str, Any]) -> None:
    if doc.get("schema") != SUMMARY_SCHEMA_ID:
        raise ValueError(f"summary must use {SUMMARY_SCHEMA_ID}")
    measurement = doc.get("measurement")
    if not isinstance(measurement, dict):
        raise ValueError("summary is missing measurement object")
    for key, expected in MEASUREMENT_CONSTS.items():
        if measurement.get(key) != expected:
            raise ValueError(f"expected measurement.{key}={expected!r}")
    for section in ("inputs", "timing"):
        if not isinstance(doc.get(section), dict):
            raise ValueError(f"summary is missing {section} object")


def build_comparison(bare: dict[str, Any], pek: dict[str, Any]) -> dict[str, Any]:
    validate_summary(bare)
    validate_summary(pek)
    if bare.get("measurement") != pek.get("measurement"):
        raise ValueError("measurement definitions differ")
    for key in COMPARABLE_INPUTS:
        if bare["inputs"].get(key) != pek["inputs"].get(key):
            raise ValueError(f"input mismatch for {key}")
    for key in ("total_frames", "measured_frames"):
        if bare["timing"].get(key) != pek["timing"].get(key):
            raise ValueError(f"timing mismatch for {key}")

    inputs = {key: bare["inputs"][key] for key in COMPARABLE_INPUTS}
    inputs["bare_model"] = bare["inputs"]["model"]
    inputs["pek_opchain"] = pek["inputs"]["opchain"]
    return {
        "schema": COMPARISON_SCHEMA,
        "measurement": bare["measurement"],
        "inputs": inputs,
        "runners": {
            "bare": {"name": bare["runner"], **bare["timing"]},
            "pek": {"name": pek["runner"], **pek["timing"]},
        },
        "fps": fps_delta(float(bare["timing"]["pipeline_fps"]), float(pek["timing"]["pipeline_fps"])),
    }


def build_report(runs_root: Path) -> dict[str, Any]:
    comparisons = [(path.parent.name, load_json(path)) for path in sorted(runs_root.glob("run-*/comparison.json"))]
    if not comparisons:
        raise ValueError(f"no run comparisons found under {runs_root}")
    if any(doc.get("schema") != COMPARISON_SCHEMA for _, doc in comparisons):
        raise ValueError(f"all comparisons must use {COMPARISON_SCHEMA}")

    first = comparisons[0][1]
    for _, doc in comparisons[1:]:
        if doc["measurement"] != first["measurement"] or doc["inputs"] != first["inputs"]:
            raise ValueError("run comparison inputs differ")
    bare_fps = statistics.median(float(doc["fps"]["bare_fps"]) for _, doc in comparisons)
    pek_fps = statistics.median(float(doc["fps"]["pek_fps"]) for _, doc in comparisons)
    return {
        "schema": REPORT_SCHEMA,
        "measurement": first["measurement"],
        "inputs": first["inputs"],
        "run_count": len(comparisons),
        "median_fps": fps_delta(bare_fps, pek_fps),
        "runs": [
            {"name": name, "bare_fps": doc["fps"]["bare_fps"], "pek_fps": doc["fps"]["pek_fps"]}
            for name, doc in comparisons
        ],
    }


def comparison_markdown(doc: dict[str, Any]) -> str:
    fps = doc["fps"]
    inputs = doc["inputs"]
    return (
        "# YOLO Video FPS Comparison\n\n"
        f"MediaPipe MP4: {inputs['source_width']}x{inputs['source_height']}, "
        f"{inputs['source_fps']:.3f} source FPS, {inputs['source_frame_count']} frames.\n\n"
        "| Metric | Bare Ultralytics | PEK | PEK delta | Ratio |\n"
        "|---|---:|---:|---:|---:|\n"
        f"| Unpaced pipeline FPS | {fps['bare_fps']:.3f} | {fps['pek_fps']:.3f} | "
        f"{fps['delta_fps']:+.3f} ({fps['delta_percent']:+.2f}%) | {fps['ratio']:.3f}x |\n\n"
        "Higher is better. Decode, color conversion, inference, post-processing, and result delivery are included.\n"
    )


def report_markdown(doc: dict[str, Any]) -> str:
    fps = doc["median_fps"]
    rows = "".join(f"| {run['name']} | {run['bare_fps']:.3f} | {run['pek_fps']:.3f} |\n" for run in doc["runs"])
    return (
        "# YOLO Video Performance\n\n"
        f"**Median unpaced FPS ({doc['run_count']} runs): Bare {fps['bare_fps']:.3f}, "
        f"PEK {fps['pek_fps']:.3f}, PEK delta {fps['delta_fps']:+.3f} "
        f"({fps['delta_percent']:+.2f}%).**\n\n"
        "| Run | Bare Ultralytics FPS | PEK FPS |\n"
        "|---|---:|---:|\n"
        f"{rows}\n"
        "The pinned video is processed unpaced; its 30 FPS timestamp rate does not cap throughput.\n"
    )


def write_outputs(doc: dict[str, Any], markdown: str, output_json: Path, output_md: Path) -> None:
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    output_md.write_text(markdown, encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.runs_root:
        doc = build_report(args.runs_root)
        markdown = report_markdown(doc)
    else:
        if not args.bare_summary or not args.pek_summary:
            raise SystemExit("--bare-summary and --pek-summary are required without --runs-root")
        doc = build_comparison(load_json(args.bare_summary), load_json(args.pek_summary))
        markdown = comparison_markdown(doc)
    write_outputs(doc, markdown, args.output_json, args.output_md)
    print(markdown, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
