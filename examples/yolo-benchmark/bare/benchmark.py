#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Run a bare Ultralytics predict loop and write the common benchmark summary."""

from __future__ import annotations

import argparse
import json
import math
import os
import time
from pathlib import Path
from typing import Any


SCHEMA_PATH = Path(__file__).resolve().parents[1] / "schema" / "benchmark_summary.schema.json"
SUMMARY_SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
SUMMARY_SCHEMA_ID = SUMMARY_SCHEMA["properties"]["schema"]["const"]
MEASUREMENT_CONSTS = {
    key: value["const"]
    for key, value in SUMMARY_SCHEMA["properties"]["measurement"]["properties"].items()
    if "const" in value
}
IMG_SIZE = 320
DEVICE = "cpu"
WARMUP_IMAGES = 1
FINGERPRINT_HEADER = "# image_set_fingerprint="


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bare YOLO predict benchmark")
    parser.add_argument("--model")
    parser.add_argument("--images", type=Path, help="TSV: image_id<TAB>image_path")
    parser.add_argument("--output", type=Path, help="Predictions JSONL")
    parser.add_argument("--summary", type=Path, help="Common benchmark_summary.json")
    return parser.parse_args()


def set_cpu_env() -> None:
    os.environ.setdefault("CUDA_VISIBLE_DEVICES", "-1")
    os.environ.setdefault("MPLBACKEND", "Agg")


def read_image_list(path: Path) -> tuple[str, list[tuple[str, str]]]:
    fingerprint = ""
    rows: list[tuple[str, str]] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip("\r")
        if line.startswith(FINGERPRINT_HEADER):
            fingerprint = line[len(FINGERPRINT_HEADER):].strip()
            continue
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            raise ValueError(f"{path}: expected image_id<TAB>image_path")
        image_id, image_path = line.split("\t", 1)
        rows.append((image_id, image_path))
    if not fingerprint:
        raise ValueError(f"{path}: missing {FINGERPRINT_HEADER} header; use prepare_dataset.py")
    return fingerprint, rows


def percentile(sorted_values: list[float], q: float) -> float:
    if not sorted_values:
        return 0.0
    index = math.ceil(q * len(sorted_values)) - 1
    return sorted_values[min(index, len(sorted_values) - 1)]


def timing_stats(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {
            "count": 0,
            "avg_ms": 0.0,
            "p50_ms": 0.0,
            "p75_ms": 0.0,
            "p95_ms": 0.0,
            "p99_ms": 0.0,
            "min_ms": 0.0,
            "max_ms": 0.0,
        }
    sorted_values = sorted(values)
    return {
        "count": len(values),
        "avg_ms": sum(values) / len(values),
        "p50_ms": percentile(sorted_values, 0.50),
        "p75_ms": percentile(sorted_values, 0.75),
        "p95_ms": percentile(sorted_values, 0.95),
        "p99_ms": percentile(sorted_values, 0.99),
        "min_ms": sorted_values[0],
        "max_ms": sorted_values[-1],
    }


def result_json(image_id: str, image_path: str, result: Any) -> dict[str, Any]:
    names = getattr(result, "names", {}) or {}
    boxes = getattr(result, "boxes", None)
    detections: list[dict[str, Any]] = []
    if boxes is not None:
        for xyxy, conf, cls in zip(boxes.xyxy.cpu().tolist(), boxes.conf.cpu().tolist(), boxes.cls.cpu().tolist()):
            class_id = int(cls)
            detections.append(
                {
                    "bbox_xyxy": [float(v) for v in xyxy],
                    "class_id": class_id,
                    "class_name": names.get(class_id, str(class_id)),
                    "confidence": float(conf),
                }
            )
    return {"image_id": image_id, "image_path": image_path, "detections": detections}


def timings_path_for(output_path: Path) -> Path:
    return output_path.with_name("timings.jsonl")


def timing_json(
    row: tuple[int, str, str, int, int, int, float, float, float, float],
) -> dict[str, Any]:
    image_index, image_id, image_path, width, height, detections, wall_ms, preprocess_ms, inference_ms, postprocess_ms = row
    return {
        "image_index": image_index,
        "image_id": image_id,
        "image_path": image_path,
        "width": width,
        "height": height,
        "detections": detections,
        "wall_ms": wall_ms,
        "preprocess_ms": preprocess_ms,
        "inference_ms": inference_ms,
        "postprocess_ms": postprocess_ms,
    }


def write_summary(
    args: argparse.Namespace,
    image_count: int,
    image_fingerprint: str,
    warmup_images: int,
    load_ms: float,
    preload_ms: float,
    loop_wall_ms: float,
    image_times_ms: list[float],
) -> None:
    if args.summary is None:
        raise ValueError("summary path is required")
    doc = {
        "schema": SUMMARY_SCHEMA_ID,
        "runner": "bare-ultralytics-predict",
        "measurement": MEASUREMENT_CONSTS | {"warmup_images": warmup_images},
        "inputs": {
            "model": args.model,
            "opchain": "",
            "image_list": str(args.images),
            "image_count": image_count,
            "image_set_fingerprint": image_fingerprint,
            "imgsz": IMG_SIZE,
            "device": DEVICE,
        },
        "outputs": {
            "predictions_jsonl": str(args.output),
            "timings_jsonl": str(timings_path_for(args.output)),
        },
        "timing": {
            "load_ms": load_ms,
            "preload_ms": preload_ms,
            "loop_wall_ms": loop_wall_ms,
            "per_image_ms": timing_stats(image_times_ms),
        },
    }
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.summary.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    if not args.model or not args.images or not args.output or not args.summary:
        raise SystemExit("--model, --images, --output and --summary are required")

    root = Path(__file__).resolve().parent
    config_dir = root / ".ultralytics"
    config_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("YOLO_CONFIG_DIR", str(config_dir))
    set_cpu_env()

    from PIL import Image  # noqa: PLC0415
    from ultralytics import YOLO  # noqa: PLC0415

    image_fingerprint, rows = read_image_list(args.images)
    started = time.perf_counter()
    model = YOLO(args.model, task="detect")
    load_ms = (time.perf_counter() - started) * 1000.0

    preload_started = time.perf_counter()
    sources = []
    for image_id, image_path in rows:
        with Image.open(image_path) as image:
            sources.append((image_id, image_path, image.convert("RGB").copy()))
    preload_ms = (time.perf_counter() - preload_started) * 1000.0

    warmup_images = min(WARMUP_IMAGES, len(rows))
    for _, _, source in sources[:warmup_images]:
        model.predict(source=source, imgsz=IMG_SIZE, device=DEVICE, verbose=False)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    image_times_ms = [0.0] * len(sources)
    timing_rows: list[tuple[int, str, str, int, int, int, float, float, float, float] | None] = [None] * len(sources)
    loop_started = time.perf_counter()
    with args.output.open("w", encoding="utf-8") as output:
        for slot, (image_id, image_path, source) in enumerate(sources):
            image_index = slot + 1
            width, height = source.size
            image_started = time.perf_counter()
            result = model.predict(source=source, imgsz=IMG_SIZE, device=DEVICE, verbose=False)[0]
            wall_ms = (time.perf_counter() - image_started) * 1000.0
            image_times_ms[slot] = wall_ms
            prediction = result_json(image_id, image_path, result)
            speed = getattr(result, "speed", {}) or {}
            timing_rows[slot] = (
                image_index,
                image_id,
                image_path,
                width,
                height,
                len(prediction["detections"]),
                wall_ms,
                float(speed.get("preprocess", 0.0)),
                float(speed.get("inference", 0.0)),
                float(speed.get("postprocess", 0.0)),
            )
            output.write(json.dumps(prediction, separators=(",", ":")) + "\n")
    loop_wall_ms = (time.perf_counter() - loop_started) * 1000.0
    with timings_path_for(args.output).open("w", encoding="utf-8") as timings:
        for row in timing_rows:
            if row is not None:
                timings.write(json.dumps(timing_json(row), separators=(",", ":")) + "\n")

    write_summary(args, len(rows), image_fingerprint, warmup_images, load_ms, preload_ms, loop_wall_ms, image_times_ms)
    print(f"processed {len(rows)} images")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
