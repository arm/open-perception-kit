#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Measure unpaced Ultralytics video throughput."""

from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path
from typing import Any


SCHEMA_PATH = Path(__file__).resolve().parents[1] / "schema" / "video_benchmark_summary.schema.json"
SUMMARY_SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
SUMMARY_SCHEMA_ID = SUMMARY_SCHEMA["properties"]["schema"]["const"]
IMG_SIZE = 320
DEVICE = "cpu"
WARMUP_FRAMES = 1
MEASUREMENT_CONSTS = {
    key: value["const"]
    for key, value in SUMMARY_SCHEMA["properties"]["measurement"]["properties"].items()
    if "const" in value
}
MEASUREMENT = {
    **MEASUREMENT_CONSTS,
    "warmup_frames": WARMUP_FRAMES,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True)
    parser.add_argument("--video", required=True, type=Path)
    parser.add_argument("--source-manifest", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    return parser.parse_args()


def load_source(path: Path) -> dict[str, Any]:
    source = json.loads(path.read_text(encoding="utf-8"))
    if source.get("schema") != "expkits_yolo_video_source.v1":
        raise ValueError(f"{path}: unsupported video source manifest")
    if int(source.get("frame_count", 0)) <= WARMUP_FRAMES:
        raise ValueError(f"{path}: frame_count must be greater than {WARMUP_FRAMES}")
    return source


def summary_document(
    args: argparse.Namespace,
    source: dict[str, Any],
    total_frames: int,
    elapsed_ms: float,
    load_ms: float,
) -> dict[str, Any]:
    expected_frames = int(source["frame_count"])
    if total_frames != expected_frames:
        raise ValueError(f"decoded {total_frames} frames, expected {expected_frames}")
    measured_frames = total_frames - WARMUP_FRAMES
    if elapsed_ms <= 0:
        raise ValueError("measured video interval must be positive")
    return {
        "schema": SUMMARY_SCHEMA_ID,
        "runner": "bare-ultralytics-video",
        "measurement": dict(MEASUREMENT),
        "inputs": {
            "model": args.model,
            "opchain": "",
            "video": str(args.video),
            "video_sha256": source["sha256"],
            "source_width": int(source["width"]),
            "source_height": int(source["height"]),
            "source_fps": float(source["fps"]),
            "source_frame_count": expected_frames,
            "imgsz": IMG_SIZE,
            "device": DEVICE,
        },
        "timing": {
            "load_ms": load_ms,
            "total_frames": total_frames,
            "measured_frames": measured_frames,
            "elapsed_ms": elapsed_ms,
            "pipeline_fps": measured_frames * 1000.0 / elapsed_ms,
        },
    }


def main() -> int:
    args = parse_args()
    source = load_source(args.source_manifest)
    os.environ.setdefault("CUDA_VISIBLE_DEVICES", "-1")
    os.environ.setdefault("MPLBACKEND", "Agg")

    from ultralytics import YOLO  # noqa: PLC0415

    started = time.perf_counter()
    model = YOLO(args.model, task="detect")
    load_ms = (time.perf_counter() - started) * 1000.0

    total_frames = 0
    first_ready = 0.0
    last_ready = 0.0
    results = model.predict(
        source=str(args.video),
        stream=True,
        batch=1,
        vid_stride=1,
        imgsz=IMG_SIZE,
        device=DEVICE,
        verbose=False,
    )
    for result in results:
        result.to_json()
        ready = time.perf_counter()
        total_frames += 1
        height, width = result.orig_shape
        if (width, height) != (int(source["width"]), int(source["height"])):
            raise ValueError(f"decoded frame is {width}x{height}, expected {source['width']}x{source['height']}")
        if total_frames == WARMUP_FRAMES:
            first_ready = ready
        elif total_frames > WARMUP_FRAMES:
            last_ready = ready

    elapsed_ms = (last_ready - first_ready) * 1000.0
    summary = summary_document(args, source, total_frames, elapsed_ms, load_ms)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.summary.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"bare video FPS: {summary['timing']['pipeline_fps']:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
