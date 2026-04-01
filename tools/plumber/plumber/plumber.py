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

from . import auxiliary as aux
from . import distance_rect as dr
from . import distance_yaw_pitch as dyp
from . import distance_seg_map as dsm
from . import distance_classification as dc
from . import distance_video_frame as dvf
from . import distance_track_trace as dtt
from . import distance_object_embedding as doe

# distance func signature: (gt_det: dict, out_det: dict, gt_parent_det, out_parent_det -> float
DistanceFn = Callable[[dict, dict, dict, dict], float]


@dataclass
class DistanceSpec:
    func: DistanceFn
    threshold: float
    skip: bool = False


DISTANCE_SPECS: Dict[str, DistanceSpec] = {
    "Rect": DistanceSpec(dr.distance_rect, threshold=0.01),
    "YawPitch": DistanceSpec(dyp.distance_yawpitch, threshold=0.01),
    "SegmentationMap": DistanceSpec(dsm.distance_segmentation_map, threshold=0.1, skip=True),
    "Classification": DistanceSpec(dc.distance_classification, threshold=0.2),
    "VideoFrame": DistanceSpec(dvf.distance_video_frame, threshold=0.1, skip=True),
    "TrackTrace": DistanceSpec(dtt.distance_track_trace, threshold=0.1, skip=False),
    "ObjectEmbedding": DistanceSpec(doe.distance_object_embedding, threshold=0.2, skip=False),
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

def process_remaining(
        i: int,
        gt: dict,
        remaining: list[dict],
        out_uuid_index: dict[int, tuple[dict, dict]],
        gt_uuid_index: dict[int, tuple[dict, dict]],
        dist_spec: DistanceSpec
) -> Tuple[bool, str]:

    best_j = -1
    best_dist = math.inf

    gt_parent_uuid = gt['data']['parentUuid']
    _, gt_parent_det = gt_uuid_index.get(gt_parent_uuid, (None, None))
    if gt_parent_det is None:
        return False, f"detection has no parent (parent uuid: {gt_parent_uuid})"

    for j, out in enumerate(remaining):

        out_parent_uuid = out['data']['parentUuid']
        _, out_parent_det = out_uuid_index.get(out_parent_uuid, (None, None))
        if out_parent_det is None:
            return False, f"detection has no parent (parent uuid: {out_parent_uuid})"

        d = dist_spec.func(gt, out, gt_parent_det, out_parent_det)

        # ensure distance is numeric and normalized
        if not (isinstance(d, (int, float)) and math.isfinite(d)):
            return False, f"distance function returned non-finite value for type at gt index {i}"
        if d < best_dist:
            best_dist = d
            best_j = j

    # enforce threshold
    if best_dist > dist_spec.threshold:
        return False, (
            f"distance too large for gt index {i}: {best_dist:.6f} > threshold {dist_spec.threshold}"
        )

    # consume match
    if best_j >= 0:
        remaining.pop(best_j)
        return True, ""
    else:
        return False, "The best match cannot be found"


def greedy_match_by_distance(
    gt_dets: List[dict],
    gt_uuid_index: dict[int, tuple[dict, dict]],
    out_dets: List[dict],
    out_uuid_index: dict[int, tuple[dict, dict]],
    dist_spec: DistanceSpec,
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

    # enumerate detections
    for i, gt in enumerate(gt_dets):
        if not remaining:
            return False, f"ran out of output detections at gt index {i}"

        ok, msg = process_remaining(i, gt, remaining, out_uuid_index, gt_uuid_index, dist_spec)
        if not ok:
            return ok, msg

    if remaining:
        return False, f"unmatched output detections remain: {len(remaining)}"

    return True, "ok"


def compare_layer(
        gt_layer: dict, gt_uuid_index: dict[int, tuple[dict, dict]],
        out_layer: dict, out_uuid_index: dict[int, tuple[dict, dict]]) -> Tuple[bool, str]:
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

    gt_by_type = aux.group_by_type(gt_dets)
    out_by_type = aux.group_by_type(out_dets)

    gt_types = set(gt_by_type.keys())
    out_types = set(out_by_type.keys())
    if gt_types != out_types:
        missing = sorted(gt_types - out_types)
        extra = sorted(out_types - gt_types)
        return False, f"detection type mismatch: missing={missing} extra={extra}"

    for det_type in sorted(gt_types):
        spec = DISTANCE_SPECS.get(det_type)
        if spec is None:
            return False, f"no distance spec for detection type {det_type!r}"

        ok, msg = greedy_match_by_distance(
            gt_by_type[det_type],
            gt_uuid_index,
            out_by_type[det_type],
            out_uuid_index,
            spec,
        )
        if not ok:
            return False, f"type {det_type!r}: {msg}"

    return True, "ok"


def compare_perception(args, gt_obj: dict, out_obj: dict, element_id_key="infer-id") -> Tuple[bool, str]:
    """
    Compare two full messages. This focuses on perception.layers.
    Strategy:
    - strip volatile fields first
    - match layers by contentType
    - within a layer:
        - if Rect detections -> special matcher (IoU + label)
        - else -> compare detections list as-is (order sensitive) with numeric tolerances
    """

    if args.verbose:
        fc = out_obj.get("frame_counter", 0)
        print(f"====================== comparing frame: {fc} =============================")

    gt_layers = aux.get_layers(gt_obj)
    out_layers = aux.get_layers(out_obj)

    gt_uuid_index = aux.build_uuid_index(gt_obj['perception'])
    out_uuid_index = aux.build_uuid_index(out_obj['perception'])

    out_idx = aux.index_layers_by_element_id(out_layers, key_name=element_id_key)

    for i, gt_layer in enumerate(gt_layers):
        element_id = gt_layer.get(element_id_key)

        if not isinstance(element_id, str) or not element_id:
            return False, f"GT layer at index {i} missing/invalid {element_id_key!r}"

        out_layer = out_idx.get(element_id)
        if out_layer is None:
            return False, f"Missing output layer for {element_id_key}={element_id!r}"

        ok, msg = compare_layer(gt_layer, gt_uuid_index, out_layer, out_uuid_index)
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
            try:
                out_obj = json.loads(line)
            except json.JSONDecodeError:
                if args.verbose:
                    print("Skipping bad JSON line")
                continue

            ok, msg = compare_perception(args, gt_obj, out_obj)
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
