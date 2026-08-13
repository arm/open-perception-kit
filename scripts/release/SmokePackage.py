#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Validate an extracted PEK release through its Python GStreamer surface."""

from __future__ import annotations

import argparse
import base64
import ctypes
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst  # noqa: E402

import ReleaseTool as release_tool  # noqa: E402

INFERENCE_TIMEOUT_SECONDS = 120
PEKCOMM_TIMEOUT_SECONDS = 5
WEB_STARTUP_TIMEOUT_SECONDS = 5
REQUIRED_ELEMENTS = {
    "fakesink",
    "opusenc",
    "pekcomm",
    "pekinfer",
    "pekosd",
    "pekperformance",
    "peksink",
    "pektracker",
    "videoconvert",
    "videotestsrc",
    "vp8enc",
    "webrtcbin",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def extract_package(archive: Path, temporary_root: Path) -> Path:
    # CI creates this local archive in the preceding package-build job; extraction uses Python's
    # data filter and an isolated temporary directory.
    with tarfile.open(archive, "r:gz") as package_archive:  # NOSONAR
        package_archive.extractall(temporary_root, filter="data")

    package_roots = [
        path
        for path in temporary_root.iterdir()
        if path.is_dir() and not path.is_symlink() and path.name.startswith("pek-")
    ]
    if len(package_roots) != 1:
        fail("Archive must contain exactly one PEK package root")
    return package_roots[0]


def validate_package(package_root: Path, architecture: str) -> None:
    release_tool.validate_package(
        argparse.Namespace(
            architecture=architecture,
            package_root=str(package_root),
            repo_root=None,
        )
    )


def initialise_gstreamer(package_root: Path, temporary_root: Path) -> None:
    os.environ.pop("LD_LIBRARY_PATH", None)
    os.environ["GST_PLUGIN_PATH"] = str(package_root / "lib/gstreamer-1.0")
    os.environ["GST_REGISTRY"] = str(temporary_root / "gstreamer-registry.bin")
    os.environ["OPK_LOG_LEVEL"] = "4"
    os.environ["OPK_LOG_TARGETS"] = "stdout"

    ctypes.CDLL(str(package_root / "lib/pek/pek-runtime.so"))
    Gst.init(None)


def validate_elements() -> None:
    missing = sorted(
        name for name in REQUIRED_ELEMENTS if Gst.ElementFactory.find(name) is None
    )
    if missing:
        fail(f"Missing GStreamer elements: {', '.join(missing)}")


def raise_bus_error(message) -> None:
    error, debug = message.parse_error()
    source = message.src.get_name() if message.src is not None else "GStreamer"
    detail = f"{source}: {error.message}"
    if debug:
        detail += f" ({debug})"
    fail(detail)


def start_pipeline(pipeline) -> None:
    if pipeline.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
        fail("GStreamer pipeline failed to enter PLAYING")


def stop_pipeline(pipeline) -> None:
    pipeline.set_state(Gst.State.NULL)


def load_perception_sdk():
    sdk_source = Path(__file__).resolve().parents[2] / "generated/perception/python/src"
    sys.path.insert(0, str(sdk_source))

    from perception.packet import Envelope  # noqa: PLC0415
    from perception.fb.perception.metadata.PerformanceOverlay import (  # noqa: PLC0415
        PerformanceOverlayT,
    )

    return Envelope, PerformanceOverlayT


def decode_pekcomm_envelope(line: bytes, envelope_type):
    try:
        payload = json.loads(line)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(payload, dict) or not isinstance(payload.get("frame_counter"), int):
        return None
    if payload.get("frame_results_encoding") != "perception-frame-results+base64":
        return None

    encoded_packet = payload.get("frame_results_packet_b64")
    if not isinstance(encoded_packet, str) or not encoded_packet:
        return None
    try:
        return envelope_type(base64.b64decode(encoded_packet, validate=True))
    except (ValueError, TypeError):
        return None


def contains_performance_overlay(line: bytes, envelope_type, performance_overlay_type) -> bool:
    envelope = decode_pekcomm_envelope(line, envelope_type)
    return envelope is not None and envelope.valid() and envelope.contains(performance_overlay_type)


def validate_pekcomm_output(output: Path) -> None:
    envelope_type, performance_overlay_type = load_perception_sdk()
    deadline = time.monotonic() + PEKCOMM_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if output.is_file() and any(
            contains_performance_overlay(line, envelope_type, performance_overlay_type)
            for line in output.read_bytes().splitlines()
        ):
            return
        time.sleep(0.1)
    fail(f"pekcomm produced no valid output within {PEKCOMM_TIMEOUT_SECONDS} seconds")


def run_inference(opchain: Path, comm_output: Path) -> None:
    pipeline = Gst.parse_launch(
        "videotestsrc pattern=ball num-buffers=5 ! "
        "video/x-raw,format=BGRA,width=320,height=320,framerate=5/1 ! "
        "pekinfer name=smoke_infer ! "
        "pekperformance show-all-metrics=true update-interval=1 ! "
        "pekcomm name=smoke_comm method=file ! "
        "pekosd enabled=true ! fakesink sync=false"
    )
    infer = pipeline.get_by_name("smoke_infer")
    if infer is None:
        fail("Inference smoke pipeline has no pekinfer element")
    infer.set_property(
        "opchain-path",
        str(opchain),
    )
    comm = pipeline.get_by_name("smoke_comm")
    if comm is None:
        fail("Inference smoke pipeline has no pekcomm element")
    comm.set_property("file-name", str(comm_output))

    try:
        start_pipeline(pipeline)
        message = pipeline.get_bus().timed_pop_filtered(
            INFERENCE_TIMEOUT_SECONDS * Gst.SECOND,
            Gst.MessageType.ERROR | Gst.MessageType.EOS,
        )
        if message is None:
            fail(f"Inference pipeline timed out after {INFERENCE_TIMEOUT_SECONDS} seconds")
        if message.type == Gst.MessageType.ERROR:
            raise_bus_error(message)
        if message.type != Gst.MessageType.EOS:
            fail("Inference pipeline stopped without EOS")
        validate_pekcomm_output(comm_output)
    finally:
        stop_pipeline(pipeline)


def read_url(path: str) -> bytes:
    with urllib.request.urlopen(f"http://127.0.0.1:9999/{path}", timeout=1) as response:
        return response.read()


def validate_web_content() -> None:
    pipeline = Gst.parse_launch(
        "videotestsrc pattern=ball is-live=true ! "
        "video/x-raw,format=BGRA,width=64,height=64,framerate=4/1 ! peksink"
    )
    bus = pipeline.get_bus()
    deadline = time.monotonic() + WEB_STARTUP_TIMEOUT_SECONDS

    try:
        start_pipeline(pipeline)
        while time.monotonic() < deadline:
            if message := bus.timed_pop_filtered(0, Gst.MessageType.ERROR):
                raise_bus_error(message)
            try:
                if b"<html" in read_url("").lower():
                    break
            except urllib.error.URLError:
                pass
            time.sleep(0.1)
        else:
            fail(f"peksink HTTP server did not start within {WEB_STARTUP_TIMEOUT_SECONDS} seconds")

        if b"window.PEK_CONFIG" not in read_url("pek-config.js"):
            fail("peksink did not serve the packaged pek-config.js")
    finally:
        stop_pipeline(pipeline)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("architecture", choices=sorted(release_tool.ARCHITECTURES))
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    if not args.archive.is_file():
        parser.error(f"Archive does not exist: {args.archive}")
    return args


def main() -> int:
    args = parse_arguments()
    with tempfile.TemporaryDirectory() as temporary:
        temporary_root = Path(temporary)
        package_root = extract_package(args.archive.resolve(), temporary_root)
        validate_package(package_root, args.architecture)
        initialise_gstreamer(package_root, temporary_root)
        validate_elements()
        run_inference(
            package_root / "share/pek/models/yolov11/opchain.json",
            temporary_root / "pekcomm-onnx.jsonl",
        )
        run_inference(
            package_root / "share/pek/models/yolox/opchain.json",
            temporary_root / "pekcomm-executorch.jsonl",
        )
        validate_web_content()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (
        GLib.Error,
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        tarfile.TarError,
        ValueError,
    ) as error:
        print(f"smoke error: {error}", file=sys.stderr)
        sys.exit(1)
