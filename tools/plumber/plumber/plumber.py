#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List, Optional

from .frame_results_compare import compare_frame_results_frame
from .frame_results_decode import FrameResultsDecodeError, FrameResultsFrame, decode_frame_results_record

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


def resolve_project_root() -> Path:
    project_root = Path(os.environ.get("OPK_PROJECT_ROOT", "/work"))
    if not project_root.is_absolute():
        raise ValueError(
            f"OPK_PROJECT_ROOT must be an absolute path: {project_root}"
        )
    return project_root


def start_pipeline(
    opk_menu: str,
    pipeline_name: str,
    extra_args: List[str],
    fifo_path: str,
    project_root: Path,
) -> subprocess.Popen:
    """
    Start 'opk-menu <pipeline_name> ...' in background.
    """

    cmd = [opk_menu, pipeline_name] + extra_args

    # Copy current environment
    env = os.environ.copy()

    # Set OPKCOMM_FILE for this subprocess only
    env["OPKCOMM_FILE"] = fifo_path
    env["OPK_PROJECT_ROOT"] = str(project_root)

    return subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
        cwd=project_root,
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


def decode_ground_truth(records: List[dict], path: str) -> List[FrameResultsFrame]:
    frames: List[FrameResultsFrame] = []
    for index, ndjson_record in enumerate(records):
        try:
            frames.append(decode_frame_results_record(ndjson_record))
        except FrameResultsDecodeError as exc:
            raise ValueError(
                f"Ground truth FrameResults decode failed at index {index} in {path}: {exc}"
            ) from exc
    return frames


# ---------- Modes ----------

def run_save_mode(args) -> int:
    # Ensure FIFO exists
    fifo = Path(args.fifo)
    if not fifo.exists():
        os.mkfifo(args.fifo)

    proc = start_pipeline(
        args.opk_menu,
        args.pipeline,
        args.opk_menu_args,
        args.fifo,
        args.project_root,
    )

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


def check_output_frame(args, index: int, ground_frame: FrameResultsFrame, out_record: dict) -> bool:
    try:
        out_frame = decode_frame_results_record(out_record)
    except FrameResultsDecodeError as exc:
        print(f"[FAIL] idx={index}: output FrameResults decode failed: {exc}")
        if args.verbose:
            print("  output:", json.dumps(out_record, indent=2, ensure_ascii=False))
        return True

    ok, message = compare_frame_results_frame(args, ground_frame, out_frame)
    if ok:
        if args.verbose:
            print(f"[OK] idx={index}: {message}")
        return False

    print(f"[FAIL] idx={index}: {message}")
    if args.verbose:
        print(
            "  ground:",
            json.dumps(ground_frame.ndjson_record, indent=2, ensure_ascii=False),
        )
        print("  output:", json.dumps(out_record, indent=2, ensure_ascii=False))
    return True


def prepare_ground_truth(args) -> Optional[List[FrameResultsFrame]]:
    fifo = Path(args.fifo)
    if not fifo.exists():
        os.mkfifo(args.fifo)

    ground_records = load_ndjson(args.file)
    if not ground_records:
        print(f"Ground truth file is empty: {args.file}", file=sys.stderr)
        return None

    try:
        return decode_ground_truth(ground_records, args.file)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return None


def compare_pipeline_output(args, ground_frames: List[FrameResultsFrame]) -> tuple[int, int]:
    failures = 0
    compared = 0
    fifo_iter = read_fifo_lines(args.fifo)
    for index, ground_frame in enumerate(ground_frames):
        try:
            out_record = json.loads(next(fifo_iter))
        except json.JSONDecodeError:
            if args.verbose:
                print("Skipping bad JSON line")
            continue

        failed = check_output_frame(args, index, ground_frame, out_record)
        compared += 1
        failures += int(failed)
        if (failed and args.fail_fast) or (args.limit and compared >= args.limit):
            break
    return failures, compared


def run_check_mode(args) -> int:
    ground_frames = prepare_ground_truth(args)
    if ground_frames is None:
        return 2

    proc = start_pipeline(
        args.opk_menu,
        args.pipeline,
        args.opk_menu_args,
        args.fifo,
        args.project_root,
    )
    try:
        failures, compared = compare_pipeline_output(args, ground_frames)
    except KeyboardInterrupt:
        print("Interrupted.")
        failures, compared = 0, 0
    finally:
        stop_process(proc)

    if failures == 0:
        print(f"PASS: compared={compared} failures=0")
        return 0
    print(f"FAIL: compared={compared} failures={failures}")
    return 1


# ---------- CLI ----------

def build_arg_parser(project_root: Optional[Path] = None) -> argparse.ArgumentParser:
    project_root = project_root or resolve_project_root()
    p = argparse.ArgumentParser(
        description="Tester tool for opk-menu pipeline FIFO FrameResults output.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    p.add_argument("pipeline", help="Pipeline name to start via opk-menu (e.g. 'onnx').")

    p.add_argument(
        "mode",
        choices=["save", "check"],
        help="Operating mode: save = record FIFO JSON to file; check = compare FIFO output to ground-truth file.",
    )

    p.add_argument(
        "file",
        help="NDJSON file path: output file in save mode, input file in check mode.",
    )

    p.add_argument(
        "--fifo",
        help="Path to FIFO used by opkcomm (default: a private per-run FIFO).",
    )
    p.add_argument(
        "--opk-menu",
        default=str(project_root / "tools/opk-menu"),
        help="Path to opk-menu executable.",
    )
    p.add_argument(
        "--opk-menu-args",
        nargs=argparse.REMAINDER,
        default=[],
        help="Extra args passed to opk-menu after the pipeline name. Example: --opk-menu-args --foo bar",
    )

    p.add_argument("--limit", type=int, default=0, help="Stop after N messages (0 = no limit).")
    p.add_argument("--verbose", action="store_true", help="Print per-message debug info.")
    p.add_argument("--fail-fast", action="store_true", help="Stop at first mismatch (check mode).")

    p.set_defaults(project_root=project_root)
    return p


def main() -> int:
    try:
        project_root = resolve_project_root()
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    args = build_arg_parser(project_root).parse_args()
    if args.limit < 0:
        print("--limit must be >= 0", file=sys.stderr)
        return 2

    # normalize limit: 0 means "no limit"
    args.limit = args.limit if args.limit != 0 else None

    if args.fifo is None:
        with tempfile.TemporaryDirectory(prefix="opkcomm-") as temp_dir:
            args.fifo = str(Path(temp_dir) / "fifo")
            return run_save_mode(args) if args.mode == "save" else run_check_mode(args)

    return run_save_mode(args) if args.mode == "save" else run_check_mode(args)


if __name__ == "__main__":
    raise SystemExit(main())
