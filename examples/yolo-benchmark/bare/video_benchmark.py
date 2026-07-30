#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Measure Ultralytics throughput over preloaded video frames."""

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


def preload_frames(video: Path, source: dict[str, Any], cv2: Any) -> list[Any]:
    capture = cv2.VideoCapture(str(video))
    if not capture.isOpened():
        raise ValueError(f"failed to open video: {video}")
    frames = []
    try:
        while True:
            available, frame = capture.read()
            if not available:
                break
            height, width = frame.shape[:2]
            if (width, height) != (int(source["width"]), int(source["height"])):
                raise ValueError(
                    f"decoded frame is {width}x{height}, "
                    f"expected {source['width']}x{source['height']}"
                )
            frames.append(frame)
    finally:
        capture.release()
    expected_frames = int(source["frame_count"])
    if len(frames) != expected_frames:
        raise ValueError(f"decoded {len(frames)} frames, expected {expected_frames}")
    return frames


def preloaded_video_source(frames: list[Any], loader_base: type, source_types: type) -> Any:
    class PreloadedVideoFrames(loader_base):
        def __init__(self) -> None:
            self.frames = frames
            self.bs = 1
            self.mode = "video"
            self.source_type = source_types(stream=True, screenshot=False, from_img=False, tensor=False)
            self.index = 0

        def __len__(self) -> int:
            return len(self.frames)

        def __iter__(self) -> Any:
            self.index = 0
            return self

        def __next__(self) -> tuple[list[str], list[Any], list[str]]:
            if self.index >= len(self.frames):
                raise StopIteration
            frame = self.frames[self.index]
            self.index += 1
            return [f"frame-{self.index:06d}"], [frame], [""]

    return PreloadedVideoFrames()


def summary_document(
    args: argparse.Namespace,
    source: dict[str, Any],
    total_frames: int,
    elapsed_ms: float,
    load_ms: float,
    preload_ms: float,
) -> dict[str, Any]:
    expected_frames = int(source["frame_count"])
    if total_frames != expected_frames:
        raise ValueError(f"processed {total_frames} frames, expected {expected_frames}")
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
            "preload_ms": preload_ms,
            "total_frames": total_frames,
            "measured_frames": measured_frames,
            "elapsed_ms": elapsed_ms,
            "pipeline_fps": measured_frames * 1000.0 / elapsed_ms,
        },
    }


def prediction_ready_times(model: Any, source: Any) -> list[float]:
    ready_times = []
    results = model.predict(
        source=source,
        stream=True,
        batch=1,
        imgsz=IMG_SIZE,
        device=DEVICE,
        verbose=False,
    )
    for result in results:
        result.to_json()
        ready_times.append(time.perf_counter())
    return ready_times


def benchmark_model(model: Any, frame_source: Any, source: dict[str, Any]) -> tuple[int, float]:
    expected_frames = int(source["frame_count"])
    warmup_times = prediction_ready_times(model, frame_source)
    if len(warmup_times) != expected_frames:
        raise ValueError(f"warm-up processed {len(warmup_times)} frames, expected {expected_frames}")

    measured_times = prediction_ready_times(model, frame_source)
    total_frames = len(measured_times)
    if total_frames != expected_frames:
        raise ValueError(f"processed {total_frames} frames, expected {expected_frames}")
    elapsed_ms = (measured_times[-1] - measured_times[WARMUP_FRAMES - 1]) * 1000.0
    return total_frames, elapsed_ms


def main() -> int:
    args = parse_args()
    source = load_source(args.source_manifest)
    os.environ.setdefault("CUDA_VISIBLE_DEVICES", "-1")
    os.environ.setdefault("MPLBACKEND", "Agg")

    import cv2  # noqa: PLC0415
    from ultralytics import YOLO  # noqa: PLC0415
    from ultralytics.data.loaders import LoadPilAndNumpy, SourceTypes  # noqa: PLC0415

    started = time.perf_counter()
    model = YOLO(args.model, task="detect")
    load_ms = (time.perf_counter() - started) * 1000.0

    started = time.perf_counter()
    frames = preload_frames(args.video, source, cv2)
    frame_source = preloaded_video_source(frames, LoadPilAndNumpy, SourceTypes)
    preload_ms = (time.perf_counter() - started) * 1000.0

    total_frames, elapsed_ms = benchmark_model(model, frame_source, source)
    summary = summary_document(args, source, total_frames, elapsed_ms, load_ms, preload_ms)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.summary.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"bare video FPS: {summary['timing']['pipeline_fps']:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
