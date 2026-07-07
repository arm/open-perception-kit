#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Compare two common YOLO benchmark summary artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "benchmark_summary.schema.json"
SUMMARY_SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
SUMMARY_SCHEMA_ID = SUMMARY_SCHEMA["properties"]["schema"]["const"]
MEASUREMENT_CONSTS = {
    key: value["const"]
    for key, value in SUMMARY_SCHEMA["properties"]["measurement"]["properties"].items()
    if "const" in value
}
METRICS = ("avg_ms", "p50_ms", "p95_ms", "p99_ms", "min_ms", "max_ms")
COMPARABLE_INPUTS = ("image_count", "image_set_fingerprint", "imgsz", "device")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bare-summary", type=Path)
    parser.add_argument("--pek-summary", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-md", type=Path)
    return parser.parse_args()


def load_summary(path: Path) -> dict[str, Any]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    if doc.get("schema") != SUMMARY_SCHEMA_ID:
        raise ValueError(f"{path}: unsupported schema {doc.get('schema')!r}")
    measurement = doc.get("measurement")
    if not isinstance(measurement, dict):
        raise ValueError(f"{path}: missing measurement object")
    for key, expected in MEASUREMENT_CONSTS.items():
        if measurement.get(key) != expected:
            raise ValueError(f"{path}: expected measurement.{key}={expected!r}")
    for section in ("inputs", "timing"):
        if not isinstance(doc.get(section), dict):
            raise ValueError(f"{path}: missing {section} object")
    if not isinstance(doc["timing"].get("per_image_ms"), dict):
        raise ValueError(f"{path}: missing timing.per_image_ms object")
    return doc


def validate_pair(bare: dict[str, Any], pek: dict[str, Any]) -> None:
    if bare["measurement"] != pek["measurement"]:
        raise ValueError("benchmark summaries use different measurement blocks")
    for key in COMPARABLE_INPUTS:
        if bare["inputs"].get(key) != pek["inputs"].get(key):
            raise ValueError(f"benchmark summaries use different inputs.{key}")
    if not bare["inputs"].get("image_set_fingerprint"):
        raise ValueError("benchmark summaries are missing inputs.image_set_fingerprint")
    if bare["inputs"]["image_count"] <= 0:
        raise ValueError("benchmark summaries contain no images")
    for name, doc in (("bare", bare), ("pek", pek)):
        count = doc["timing"]["per_image_ms"]["count"]
        expected = doc["inputs"]["image_count"]
        if count != expected:
            raise ValueError(f"{name}: per-image count {count} does not match image_count {expected}")


def delta_row(bare_value: float, pek_value: float) -> dict[str, float | None]:
    if bare_value < 0:
        raise ValueError(f"bare metric must be non-negative, got {bare_value}")
    ratio = None if bare_value == 0 else pek_value / bare_value
    return {
        "bare_ms": bare_value,
        "pek_ms": pek_value,
        "delta_ms": pek_value - bare_value,
        "ratio": ratio,
        "delta_percent": None if ratio is None else (ratio - 1.0) * 100.0,
    }


def build_comparison(bare: dict[str, Any], pek: dict[str, Any]) -> dict[str, Any]:
    validate_pair(bare, pek)
    return {
        "measurement": bare["measurement"],
        "inputs": {
            "image_count": bare["inputs"]["image_count"],
            "imgsz": bare["inputs"]["imgsz"],
            "device": bare["inputs"]["device"],
            "bare_model": bare["inputs"]["model"],
            "pek_opchain": pek["inputs"]["opchain"],
        },
        "runners": {
            "bare": bare["runner"],
            "pek": pek["runner"],
        },
        "timing_delta": {
            "load_ms": delta_row(float(bare["timing"]["load_ms"]), float(pek["timing"]["load_ms"])),
            "preload_ms": delta_row(float(bare["timing"]["preload_ms"]), float(pek["timing"]["preload_ms"])),
            "loop_wall_ms": delta_row(float(bare["timing"]["loop_wall_ms"]), float(pek["timing"]["loop_wall_ms"])),
            "per_image_ms": {
                metric: delta_row(
                    float(bare["timing"]["per_image_ms"][metric]),
                    float(pek["timing"]["per_image_ms"][metric]),
                )
                for metric in METRICS
            },
        },
    }


def markdown_table(comparison: dict[str, Any]) -> str:
    rows = [
        "# YOLO Benchmark Comparison",
        "",
        f"- Measurement: `{comparison['measurement']['timed_region']}`",
        f"- Images: `{comparison['inputs']['image_count']}`",
        f"- Image size: `{comparison['inputs']['imgsz']}`",
        f"- Device: `{comparison['inputs']['device']}`",
        "",
        "| metric | bare ms | PEK ms | delta ms | ratio | delta % |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for metric in ("avg_ms", "p50_ms", "p95_ms", "p99_ms"):
        item = comparison["timing_delta"]["per_image_ms"][metric]
        ratio = "n/a" if item["ratio"] is None else f"{item['ratio']:.3f}x"
        delta_percent = "n/a" if item["delta_percent"] is None else f"{item['delta_percent']:.1f}%"
        rows.append(
            f"| {metric} | {item['bare_ms']:.3f} | {item['pek_ms']:.3f} | "
            f"{item['delta_ms']:.3f} | {ratio} | {delta_percent} |"
        )
    rows.append("")
    return "\n".join(rows)


def write_outputs(comparison: dict[str, Any], output_json: Path, output_md: Path) -> None:
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(comparison, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    output_md.write_text(markdown_table(comparison), encoding="utf-8")


def main() -> int:
    args = parse_args()
    if not args.bare_summary or not args.pek_summary or not args.output_json or not args.output_md:
        raise SystemExit("--bare-summary, --pek-summary, --output-json and --output-md are required")

    comparison = build_comparison(load_summary(args.bare_summary), load_summary(args.pek_summary))
    write_outputs(comparison, args.output_json, args.output_md)
    print(f"wrote {args.output_json}")
    print(f"wrote {args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
