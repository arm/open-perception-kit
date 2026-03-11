#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


import argparse
import json
import os
import signal
import subprocess
import sys
import time
import math
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional, Callable

# distance func signature: (gt_det: dict, out_det: dict, frame_size: Optional[Tuple[int,int]]) -> float
DistanceFn = Callable[[dict, dict, Optional[Tuple[int, int]]], float]


@dataclass
class DistanceSpec:
    func: DistanceFn
    threshold: float
    skip: bool = False


def get_frame_size(layer: dict) -> Optional[Tuple[int, int]]:
    """
    Look for a layerProperties entry of type "FrameSize" and return (w,h) as ints.
    Returns None if not present or malformed.
    """
    for prop in (layer.get("layerProperties") or []):
        if prop.get("type") == "FrameSize":
            data = prop.get("data", {})
            w = data.get("w")
            h = data.get("h")
            try:
                if w is not None and h is not None:
                    return (int(w), int(h))
            except (ValueError, TypeError):
                pass
    return None


def _clamp01(v: float) -> float:
    return max(0.0, min(1.0, v))

# normalize rect fields to [x0,y0,x1,y1] in relative coordinates (0..1)


def _normalize_rect(det: dict, frame_size: Optional[Tuple[int, int]]) -> Tuple[float, float, float, float]:
    data = det.get("data", {})
    x = float(data.get("x", 0.0))
    y = float(data.get("y", 0.0))
    w = float(data.get("width", 0.0))
    h = float(data.get("height", 0.0))

    if frame_size is not None:
        fw, fh = frame_size
        if fw > 0 and fh > 0:
            nx = x / fw
            ny = y / fh
            nw = w / fw
            nh = h / fh
        else:
            # fallback: avoid division by zero
            nx, ny, nw, nh = x, y, w, h
    else:
        # no frame size: heuristically normalize using width/height if nonzero
        # but IoU does not require normalization, so just compute relative to the larger of w/h
        nx, ny, nw, nh = x, y, w, h

    # convert (x,y,width,height) -> (x0,y0,x1,y1)
    x0 = _clamp01(nx)
    y0 = _clamp01(ny)
    x1 = _clamp01(nx + nw)
    y1 = _clamp01(ny + nh)
    return (x0, y0, x1, y1)


def _iou_from_normalized(a: Tuple[float, float, float, float], b: Tuple[float, float, float, float]) -> float:
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b

    inter_x0 = max(ax0, bx0)
    inter_y0 = max(ay0, by0)
    inter_x1 = min(ax1, bx1)
    inter_y1 = min(ay1, by1)

    iw = max(0.0, inter_x1 - inter_x0)
    ih = max(0.0, inter_y1 - inter_y0)
    inter = iw * ih
    area_a = max(0.0, (ax1 - ax0)) * max(0.0, (ay1 - ay0))
    area_b = max(0.0, (bx1 - bx0)) * max(0.0, (by1 - by0))
    denom = area_a + area_b - inter
    if denom <= 0.0:
        return 0.0
    return inter / denom


def distance_rect(gt_det: dict, out_det: dict, frame_size: Optional[Tuple[int, int]]) -> float:
    """
    Return a normalized distance in [0,1] for Rect detections.
    We use: distance = 1 - IoU(normalized boxes).
    If either rect has zero area, IoU=0 (distance=1).
    """
    a = _normalize_rect(gt_det, frame_size)
    b = _normalize_rect(out_det, frame_size)
    iou = _iou_from_normalized(a, b)
    dist = 1.0 - iou
    # clamp for numerical safety
    return max(0.0, min(1.0, dist))


def distance_yawpitch(gt_det: dict, out_det: dict, frame_size: Optional[Tuple[int, int]]) -> float:
    """
    Normalize yaw/pitch difference into [0,1].
    For now return normalized max(|yaw_diff|, |pitch_diff|) / 180 (safe upper bound).
    We'll refine tomorrow.
    """
    g = gt_det.get("data", {})
    o = out_det.get("data", {})
    yaw_diff = abs(float(g.get("yaw", 0.0)) - float(o.get("yaw", 0.0)))
    pitch_diff = abs(float(g.get("pitch", 0.0)) - float(o.get("pitch", 0.0)))
    # angles are in degrees-ish; use 180 as safe normalization factor
    return _clamp01(max(yaw_diff, pitch_diff) / 180.0)


def distance_segmentation_map(gt_det: dict, out_det: dict, frame_size: Optional[Tuple[int, int]]) -> float:
    """
    Placeholder: return 1.0 (max distance). We'll implement mask IoU later.
    """
    return 1.0


def distance_classification(gt_det: dict, out_det: dict, frame_size: Optional[Tuple[int, int]]) -> float:
    """
    Placeholder: compare top-1 text equality (0 if equal else 1).
    We'll implement normalized candidate overlap later.
    """
    g_cands = gt_det.get("data", {}).get("candidates", [])
    o_cands = out_det.get("data", {}).get("candidates", [])
    if not g_cands or not o_cands:
        return 1.0
    gt_top = g_cands[0].get("text")
    out_top = o_cands[0].get("text")
    return 0.0 if gt_top == out_top else 1.0


DISTANCE_SPECS: Dict[str, DistanceSpec] = {
    "Rect": DistanceSpec(distance_rect, threshold=10.0),
    "YawPitch": DistanceSpec(distance_yawpitch, threshold=5.0),
    "SegmentationMap": DistanceSpec(distance_segmentation_map, threshold=0.1, skip=True),
    "Classification": DistanceSpec(distance_classification, threshold=0.2),
}

# ---------- FIFO reading ----------


def read_fifo_lines(fifo_path: str):
    """
    Generator yielding complete lines from FIFO.
    Reopens FIFO if writer disconnects.
    """
    fifo = Path(fifo_path)

    if not fifo.exists():
        raise FileNotFoundError(f"FIFO does not exist: {fifo_path}")
    if not fifo.is_fifo():
        raise ValueError(f"Path exists but is not a FIFO: {fifo_path}")

    while True:
        with fifo.open("r", encoding="utf-8", newline="") as f:
            for line in f:
                line = line.strip()
                if line:
                    yield line
        # writer closed; reopen and wait for next writer
        time.sleep(0.05)


# ---------- Subprocess management ----------

def start_pipeline(amp_menu: str, pipeline_name: str, extra_args: List[str], fifo_path: str) -> subprocess.Popen:
    """
    Start 'amp-menu <pipeline_name> ...' in background.
    """

    cmd = [amp_menu, pipeline_name] + extra_args

    # Copy current environment
    env = os.environ.copy()

    # Set AMPCOMM_FILE for this subprocess only
    env["AMPCOMM_FILE"] = fifo_path

    return subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
        cwd="/work/",
        env=env,
    )


def stop_process(proc: Optional[subprocess.Popen], kill_after_s: float = 3.0) -> None:
    if not proc:
        return
    if proc.poll() is not None:
        return
    try:
        proc.terminate()
        proc.wait(timeout=kill_after_s)
    except subprocess.TimeoutExpired:
        proc.kill()


# ---------- Ground-truth I/O ----------

def write_ndjson_line(fp, obj: dict) -> None:
    fp.write(json.dumps(obj, separators=(",", ":"), ensure_ascii=False))
    fp.write("\n")
    fp.flush()


def load_ndjson(path: str) -> List[dict]:
    out: List[dict] = []
    with open(path, "r", encoding="utf-8") as f:
        for i, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError as e:
                raise ValueError(f"Bad JSON on line {i} in {path}: {e}") from e
    return out


# ---------- Comparison logic ----------

def _get_layers(obj: dict) -> List[dict]:
    return (((obj.get("perception") or {}).get("layers")) or [])


def _index_layers_by_element_id(layers: List[dict], key_name: str = "element-id") -> Dict[str, dict]:
    """
    Build a map: element-id -> layer.
    If duplicates exist, last wins (you can change to error if you want).
    """
    idx: Dict[str, dict] = {}
    for layer in layers:
        element_id = layer.get(key_name)
        if isinstance(element_id, str) and element_id:
            idx[element_id] = layer
    return idx


def _group_by_type(dets: List[dict]) -> Dict[str, List[dict]]:
    grouped: Dict[str, List[dict]] = {}
    for d in dets or []:
        t = d.get("type", "")
        grouped.setdefault(t, []).append(d)
    return grouped


def _greedy_match_by_distance(
    gt_dets: List[dict],
    out_dets: List[dict],
    dist_spec: DistanceSpec,
    frame_size: Optional[Tuple[int, int]],
) -> Tuple[bool, str]:
    """
    Greedy matching with threshold and skip support.
    - If dist_spec.skip is True, we accept the type without checking distances.
    - Currently requires len(gt_dets) == len(out_dets). Can be relaxed later.
    """
    if dist_spec.skip:
        return True, "skipped by config"

    if len(gt_dets) != len(out_dets):
        return False, f"count mismatch: gt={len(gt_dets)} out={len(out_dets)}"

    remaining = list(out_dets)

    for i, gt in enumerate(gt_dets):
        if not remaining:
            return False, f"ran out of output detections at gt index {i}"

        best_j = -1
        best_dist = math.inf

        for j, out in enumerate(remaining):
            d = dist_spec.func(gt, out, frame_size)
            print(f"measured distance: {d}")
            # ensure distance is numeric and normalized
            if not (isinstance(d, (int, float)) and math.isfinite(d)):
                return False, f"distance function returned non-finite value for type at gt index {i}"
            if d < best_dist:
                best_dist = d
                best_j = j

        print(f"best_distance: {best_dist}")

        # enforce threshold
        if best_dist > dist_spec.threshold:
            return False, (
                f"distance too large for gt index {i}: {best_dist:.6f} > threshold {dist_spec.threshold}"
            )

        # consume match
        if best_j >= 0:
            remaining.pop(best_j)

    if remaining:
        return False, f"unmatched output detections remain: {len(remaining)}"

    return True, "ok"


def compare_layer(gt_layer: dict, out_layer: dict) -> Tuple[bool, str]:
    """
    - check contentType and model
    - group detections by type
    - for each type, use DISTANCE_SPECS[type] with greedy matching
    """
    print(f"================== layer: {out_layer['infer-id']}")
    if gt_layer.get("contentType", "") != out_layer.get("contentType", ""):
        return False, (
            f"contentType mismatch: gt={gt_layer.get('contentType')!r} "
            f"out={out_layer.get('contentType')!r}"
        )

    if gt_layer.get("model", "") != out_layer.get("model", ""):
        return False, (
            f"model mismatch: gt={gt_layer.get('model')!r} "
            f"out={out_layer.get('model')!r}"
        )

    gt_dets = gt_layer.get("detections", []) or []
    out_dets = out_layer.get("detections", []) or []

    gt_by_type = _group_by_type(gt_dets)
    out_by_type = _group_by_type(out_dets)

    gt_types = set(gt_by_type.keys())
    out_types = set(out_by_type.keys())
    if gt_types != out_types:
        missing = sorted(gt_types - out_types)
        extra = sorted(out_types - gt_types)
        return False, f"detection type mismatch: missing={missing} extra={extra}"

    # get frame size from GT layer properties (preferred)
    frame_size = get_frame_size(gt_layer)

    for det_type in sorted(gt_types):
        spec = DISTANCE_SPECS.get(det_type)
        if spec is None:
            return False, f"no distance spec for detection type {det_type!r}"

        ok, msg = _greedy_match_by_distance(
            gt_by_type[det_type],
            out_by_type[det_type],
            spec,
            frame_size,
        )
        if not ok:
            return False, f"type {det_type!r}: {msg}"

    return True, "ok"


def compare_perception(gt_obj: dict, out_obj: dict, element_id_key="infer-id") -> Tuple[bool, str]:
    """
    Compare two full messages. This focuses on perception.layers.
    Strategy:
    - strip volatile fields first
    - match layers by contentType
    - within a layer:
        - if Rect detections -> special matcher (IoU + label)
        - else -> compare detections list as-is (order sensitive) with numeric tolerances
    """
    print(f"====================== comparing frame: {out_obj['frame_counter']} =============================")
    gt_layers = _get_layers(gt_obj)
    out_layers = _get_layers(out_obj)

    out_idx = _index_layers_by_element_id(out_layers, key_name=element_id_key)

    for i, gt_layer in enumerate(gt_layers):
        element_id = gt_layer.get(element_id_key)

        if not isinstance(element_id, str) or not element_id:
            return False, f"GT layer at index {i} missing/invalid {element_id_key!r}"

        out_layer = out_idx.get(element_id)
        if out_layer is None:
            return False, f"Missing output layer for {element_id_key}={element_id!r}"

        ok, msg = compare_layer(gt_layer, out_layer)
        if not ok:
            return False, f"Layer {element_id_key}={element_id!r} mismatch: {msg}"

    return True, f"OK: matched {len(gt_layers)} layer(s) by {element_id_key}"


# ---------- Modes ----------

def run_save_mode(args) -> int:
    # Ensure FIFO exists
    fifo = Path(args.fifo)
    if not fifo.exists():
        os.mkfifo(args.fifo)

    proc = start_pipeline(args.amp_menu, args.pipeline, args.amp_menu_args, args.fifo)

    def shutdown(*_):
        stop_process(proc)
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    count = 0
    with open(args.file, "w", encoding="utf-8") as out_f:
        for line in read_fifo_lines(args.fifo):
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                if args.verbose:
                    print("Skipping bad JSON line")
                continue

            write_ndjson_line(out_f, obj)
            count += 1

            if args.limit and count >= args.limit:
                break

    stop_process(proc)
    print(f"Saved {count} JSON messages to {args.file}")
    return 0


def run_check_mode(args) -> int:
    # Ensure FIFO exists
    fifo = Path(args.fifo)
    if not fifo.exists():
        os.mkfifo(args.fifo)

    ground = load_ndjson(args.file)
    if not ground:
        print(f"Ground truth file is empty: {args.file}", file=sys.stderr)
        return 2

    proc = start_pipeline(args.amp_menu, args.pipeline, args.amp_menu_args, args.fifo)

    failures = 0
    compared = 0

    try:
        fifo_iter = read_fifo_lines(args.fifo)
        for i, gt_obj in enumerate(ground):
            # Read next output JSON from FIFO
            line = next(fifo_iter)
            out_obj = json.loads(line)

            ok, msg = compare_perception(gt_obj, out_obj)
            compared += 1
            if not ok:
                failures += 1
                print(f"[FAIL] idx={i}: {msg}")
                if args.verbose:
                    print("  ground:", json.dumps(gt_obj, indent=2, ensure_ascii=False))
                    print("  output:", json.dumps(out_obj, indent=2, ensure_ascii=False))
                if args.fail_fast:
                    break
            else:
                if args.verbose:
                    print(f"[OK] idx={i}: {msg}")

            if args.limit and compared >= args.limit:
                break

    except KeyboardInterrupt:
        print("Interrupted.")
    finally:
        stop_process(proc)

    if failures == 0:
        print(f"PASS: compared={compared} failures=0")
        return 0
    else:
        print(f"FAIL: compared={compared} failures={failures}")
        return 1


# ---------- CLI ----------

def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Tester tool for amp-menu pipeline FIFO perception output.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    p.add_argument("pipeline", help="Pipeline name to start via amp-menu (e.g. 'onnx').")

    p.add_argument(
        "mode",
        choices=["save", "check"],
        help="Operating mode: save = record FIFO JSON to file; check = compare FIFO output to ground-truth file.",
    )

    p.add_argument(
        "file",
        help="NDJSON file path: output file in save mode, input file in check mode.",
    )

    p.add_argument("--fifo", default="/tmp/ampcomm", help="Path to FIFO used by ampcomm.")
    p.add_argument("--amp-menu", default="/work/scripts/amp-menu", help="Path to amp-menu executable.")
    p.add_argument(
        "--amp-menu-args",
        nargs=argparse.REMAINDER,
        default=[],
        help="Extra args passed to amp-menu after the pipeline name. Example: --amp-menu-args --foo bar",
    )

    p.add_argument("--limit", type=int, default=0, help="Stop after N messages (0 = no limit).")
    p.add_argument("--verbose", action="store_true", help="Print per-message debug info.")
    p.add_argument("--fail-fast", action="store_true", help="Stop at first mismatch (check mode).")

    p.set_defaults(allow_extra_detections=True)

    return p


def main() -> int:
    args = build_arg_parser().parse_args()
    if args.limit < 0:
        print("--limit must be >= 0", file=sys.stderr)
        return 2

    # normalize limit: 0 means "no limit"
    args.limit = args.limit if args.limit != 0 else None

    if args.mode == "save":
        return run_save_mode(args)
    else:
        return run_check_mode(args)


if __name__ == "__main__":
    raise SystemExit(main())
