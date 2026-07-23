#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Download the pinned MediaPipe object-detection benchmark video."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from prepare_dataset import download


SOURCE_REVISION = "0ad5a71bcdff3d756dc5b07f93765aaeb4152538"  # pragma: allowlist secret
SOURCE_URL = (
    "https://raw.githubusercontent.com/google-ai-edge/mediapipe/"
    f"{SOURCE_REVISION}/mediapipe/examples/desktop/object_detection/test_video.mp4"
)
SOURCE_SHA256 = "710831289c00251c86eafb0b0d11cf6190fda02f95bcfaa5037511eabb5c9de8"  # pragma: allowlist secret
SOURCE_WIDTH = 1920
SOURCE_HEIGHT = 1080
SOURCE_FPS = 30.0
SOURCE_FRAME_COUNT = 205


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--video", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    return parser.parse_args()


def source_manifest(video: Path) -> dict[str, object]:
    return {
        "schema": "expkits_yolo_video_source.v1",
        "name": "MediaPipe object detection test video",
        "repository": "https://github.com/google-ai-edge/mediapipe",
        "revision": SOURCE_REVISION,
        "url": SOURCE_URL,
        "license": "Apache-2.0",
        "sha256": SOURCE_SHA256,
        "video": str(video),
        "width": SOURCE_WIDTH,
        "height": SOURCE_HEIGHT,
        "fps": SOURCE_FPS,
        "frame_count": SOURCE_FRAME_COUNT,
    }


def main() -> int:
    args = parse_args()
    download(SOURCE_URL, args.video, SOURCE_SHA256)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(source_manifest(args.video), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote video source manifest to {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
