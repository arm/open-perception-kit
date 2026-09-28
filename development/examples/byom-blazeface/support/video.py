# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

"""Render validated normalized face rectangles with standard FFmpeg tools."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

from support.results import Face
from support.runtime import ExampleError, ShutdownState, run_managed_command


PARTIAL_VIDEO_NAME = ".blazeface-detections.part.mp4"


def render_video(
    ffmpeg: Path,
    ffprobe: Path,
    source_video: Path,
    output_video: Path,
    filter_path: Path,
    frames: dict[int, tuple[Face, ...]],
    shutdown: ShutdownState,
) -> int:
    source_frame_count = _source_frame_count(ffprobe, source_video, shutdown)
    observed_frames = set(frames)
    missing_frames = sorted(set(range(source_frame_count)) - observed_frames)
    if missing_frames:
        print(
            f"Warning: no metadata record for {len(missing_frames)} of "
            f"{source_frame_count} source frames; those frames will be unannotated.",
            file=sys.stderr,
        )
    unexpected_frames = sorted(frame for frame in observed_frames if frame >= source_frame_count)
    if unexpected_frames:
        raise ExampleError(
            f"metadata frame {unexpected_frames[0]} exceeds the source frame count "
            f"{source_frame_count}"
        )

    _write_filter(filter_path, frames)
    partial_video = output_video.with_name(PARTIAL_VIDEO_NAME)
    partial_video.unlink(missing_ok=True)
    command = [
        str(ffmpeg),
        "-hide_banner",
        "-loglevel",
        "warning",
        "-stats",
        "-y",
        "-i",
        str(source_video),
        "-/filter:v",
        str(filter_path),
        "-map",
        "0:v:0",
        "-an",
        "-sn",
        "-dn",
        "-c:v",
        "libx264",
        "-preset",
        "fast",
        "-crf",
        "18",
        "-pix_fmt",
        "yuv420p",
        "-movflags",
        "+faststart",
        "-f",
        "mp4",
        str(partial_video),
    ]
    print(f"Rendering annotated video: {output_video}", file=sys.stderr)
    try:
        status = run_managed_command(
            command,
            shutdown,
            "ffmpeg",
            stdout=subprocess.DEVNULL,
            stderr=sys.stderr,
        )
        if status == 130:
            return status
        if status != 0:
            raise ExampleError(f"ffmpeg rendering failed with exit status {status}")
        if not partial_video.is_file() or partial_video.stat().st_size == 0:
            raise ExampleError("ffmpeg did not create a non-empty annotated video")
        os.replace(partial_video, output_video)
    finally:
        partial_video.unlink(missing_ok=True)
    print(f"Annotated video ready: {output_video}", file=sys.stderr)
    return 0


def _source_frame_count(ffprobe: Path, video: Path, shutdown: ShutdownState) -> int:
    command = [
        str(ffprobe),
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=nb_frames",
        "-of",
        "default=nokey=1:noprint_wrappers=1",
        str(video),
    ]
    try:
        result = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ExampleError(f"failed to inspect source video with ffprobe: {exc}") from exc
    shutdown.check()
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit status {result.returncode}"
        raise ExampleError(f"ffprobe failed: {detail}")
    try:
        frame_count = int(result.stdout.strip())
    except ValueError as exc:
        raise ExampleError(
            f"ffprobe returned an invalid frame count: {result.stdout.strip()!r}"
        ) from exc
    if frame_count <= 0:
        raise ExampleError(f"ffprobe returned an invalid frame count: {frame_count}")
    return frame_count


def _write_filter(path: Path, frames: dict[int, tuple[Face, ...]]) -> None:
    filters = []
    for frame, faces in frames.items():
        for face in faces:
            filters.append(
                "drawbox="
                f"x={face.x:.9f}*iw:"
                f"y={face.y:.9f}*ih:"
                f"w={face.width:.9f}*iw:"
                f"h={face.height:.9f}*ih:"
                "color=lime@0.9:t=3:"
                f"enable='eq(n,{frame})'"
            )
    path.write_text(",".join(filters) if filters else "null", encoding="utf-8")
