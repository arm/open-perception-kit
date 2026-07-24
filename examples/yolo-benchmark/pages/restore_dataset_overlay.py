#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Restores deploy-only YOLO benchmark dataset files into a Pages site snapshot.
################################################################

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


DATASET_ROOT = "yolo-performance-datasets"
DATASET_NAME = "COCO val2017"
FINGERPRINT_HEADER = "# image_set_fingerprint="
VIDEO_DATASET_NAME = "MediaPipe object detection"
VIDEO_FILENAME = "mediapipe-object-detection.mp4"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site-dir", required=True, type=Path)
    parser.add_argument("--cache-dir", required=True, type=Path)
    parser.add_argument("--image-list", type=Path)
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def dataset_id(fingerprint: str) -> str:
    if not fingerprint.startswith("sha256:"):
        raise ValueError(f"Unsupported image set fingerprint: {fingerprint}")
    return f"coco-val2017-{fingerprint.removeprefix('sha256:')[:12]}"


def video_dataset_id(digest: str) -> str:
    if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise ValueError(f"Unsupported video SHA-256: {digest}")
    return f"mediapipe-object-detection-{digest[:12]}"


def prepare_image_list(cache_dir: Path, image_list: Path | None) -> Path:
    if image_list is not None:
        return image_list

    output = cache_dir / "images.tsv"
    subprocess.run(
        [
            sys.executable,
            str(repo_root() / "examples/yolo-benchmark/prepare_dataset.py"),
            "--coco-dir",
            str(cache_dir / "coco"),
            "--output",
            str(output),
        ],
        check=True,
    )
    return output


def coco_image_root(cache_dir: Path) -> Path:
    for candidate in (cache_dir / "coco" / "images" / "val2017", cache_dir / "coco" / "val2017"):
        if candidate.is_dir():
            return candidate
    image_list = prepare_image_list(cache_dir, None)
    for candidate in (cache_dir / "coco" / "images" / "val2017", cache_dir / "coco" / "val2017"):
        if candidate.is_dir():
            return candidate
    raise FileNotFoundError(f"COCO image directory missing after preparing {image_list}")


def read_image_list(path: Path) -> tuple[str, list[tuple[str, Path]]]:
    fingerprint = ""
    rows = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip("\r")
        if line.startswith(FINGERPRINT_HEADER):
            fingerprint = line[len(FINGERPRINT_HEADER):].strip()
            continue
        if not line or line.startswith("#"):
            continue
        image_id, image_path = line.split("\t", 1)
        rows.append((image_id, Path(image_path)))
    if not fingerprint:
        raise ValueError(f"{path}: missing {FINGERPRINT_HEADER} header")
    return fingerprint, rows


def coco_image_name(image_id: str) -> str:
    return f"{int(image_id):012d}.jpg"


def report_image_lists(site_dir: Path) -> list[Path]:
    return sorted(path for path in (site_dir / "yolo-benchmark").rglob("images.tsv") if path.is_file())


def report_video_manifests(site_dir: Path) -> list[Path]:
    return sorted(path for path in (site_dir / "yolo-benchmark").rglob("video-source.json") if path.is_file())


def write_index(path: Path, title: str, links: list[tuple[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = "\n".join(
        f'        <li><a href="{html.escape(href, quote=True)}">{html.escape(text)}</a></li>'
        for text, href in links
    )
    path.write_text(
        f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{html.escape(title)}</title>
    <style>
      body {{ margin: 0; font: 16px system-ui, sans-serif; background: #0d1117; color: #e6edf3; }}
      main {{ max-width: 980px; margin: 0 auto; padding: 40px 24px; }}
      a {{ color: #3fb950; }}
      li {{ margin: 6px 0; }}
    </style>
  </head>
  <body>
    <main>
      <h1>{html.escape(title)}</h1>
      <ul>
{rows}
      </ul>
    </main>
  </body>
</html>
""",
        encoding="utf-8",
    )


def prepare_video_source(cache_dir: Path) -> tuple[Path, dict[str, object]]:
    video = cache_dir / "media" / VIDEO_FILENAME
    manifest = cache_dir / "media" / "video-source.json"
    subprocess.run(
        [
            sys.executable,
            str(repo_root() / "examples/yolo-benchmark/prepare_video.py"),
            "--video",
            str(video),
            "--manifest",
            str(manifest),
        ],
        check=True,
    )
    return video, json.loads(manifest.read_text(encoding="utf-8"))


def write_video_index(path: Path, source: dict[str, object]) -> None:
    name = html.escape(str(source.get("name", VIDEO_DATASET_NAME)))
    source_url = html.escape(str(source["url"]), quote=True)
    repository = html.escape(str(source.get("repository", "")), quote=True)
    license_name = html.escape(str(source.get("license", "")))
    metadata = html.escape(
        f'{source.get("width", "?")}x{source.get("height", "?")} @ '
        f'{source.get("fps", "?")} FPS | {source.get("frame_count", "?")} frames'
    )
    repository_link = f' | <a href="{repository}">repository</a>' if repository else ""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{name}</title>
    <style>
      body {{ margin: 0; font: 16px system-ui, sans-serif; background: #0d1117; color: #e6edf3; }}
      main {{ max-width: 1100px; margin: 0 auto; padding: 40px 24px; }}
      video {{ display: block; width: 100%; height: auto; margin: 24px 0; background: #000; }}
      a {{ color: #3fb950; }}
    </style>
  </head>
  <body>
    <main>
      <h1>{name}</h1>
      <p>{metadata}</p>
      <video controls preload="metadata" playsinline src="{VIDEO_FILENAME}"></video>
      <p><a href="{VIDEO_FILENAME}">Download input MP4</a> | <a href="{source_url}">source file</a>{repository_link}</p>
      <p>License: {license_name}</p>
    </main>
  </body>
</html>
""",
        encoding="utf-8",
    )


def restore_one_dataset(site_dir: Path, cache_dir: Path, resolved_image_list: Path) -> Path:
    fingerprint, rows = read_image_list(resolved_image_list)
    dataset = dataset_id(fingerprint)
    root = site_dir / DATASET_ROOT
    target = root / dataset
    images_dir = target / "images"

    if target.exists():
        shutil.rmtree(target)
    images_dir.mkdir(parents=True)

    image_links = []
    image_root = coco_image_root(cache_dir)
    for image_id, _artifact_source in rows:
        source = image_root / coco_image_name(image_id)
        if not source.is_file():
            raise FileNotFoundError(source)
        destination = images_dir / source.name
        shutil.copy2(source, destination)
        image_links.append((image_id, f"images/{source.name}"))

    manifest = {
        "dataset": DATASET_NAME,
        "fingerprint": fingerprint,
        "image_count": len(rows),
        "images_path": "images",
    }
    target.mkdir(parents=True, exist_ok=True)
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_index(target / "index.html", f"{DATASET_NAME} {fingerprint[:19]}", image_links)
    return target


def restore_one_video_dataset(site_dir: Path, cache_dir: Path, source: dict[str, object]) -> Path | None:
    digest = str(source.get("sha256", ""))
    dataset = video_dataset_id(digest)
    cached_video, canonical = prepare_video_source(cache_dir)
    if source.get("schema") != canonical.get("schema") or digest != canonical.get("sha256"):
        print("Skipping unsupported video source manifest; schema or SHA-256 differs")
        return None

    target = site_dir / DATASET_ROOT / dataset
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    shutil.copy2(cached_video, target / VIDEO_FILENAME)
    canonical["video"] = VIDEO_FILENAME
    (target / "manifest.json").write_text(
        json.dumps(canonical, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_video_index(target / "index.html", canonical)
    return target


def restore_overlay(site_dir: Path, cache_dir: Path, image_list: Path | None = None) -> list[Path]:
    image_lists = [prepare_image_list(cache_dir, image_list)] if image_list else report_image_lists(site_dir)
    video_manifests = report_video_manifests(site_dir)

    targets = []
    seen = set()
    for path in image_lists:
        fingerprint, _rows = read_image_list(path)
        dataset = dataset_id(fingerprint)
        if dataset in seen:
            continue
        seen.add(dataset)
        targets.append(restore_one_dataset(site_dir, cache_dir, path))
    for path in video_manifests:
        try:
            source = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(source, dict):
                raise ValueError("manifest must be an object")
            dataset = video_dataset_id(str(source.get("sha256", "")))
        except (OSError, ValueError) as error:
            print(f"Skipping invalid video source manifest {path}: {error}")
            continue
        if dataset in seen:
            continue
        target = restore_one_video_dataset(site_dir, cache_dir, source)
        if target is not None:
            seen.add(dataset)
            targets.append(target)
    write_index(
        site_dir / DATASET_ROOT / "index.html",
        "YOLO Performance Datasets",
        [(target.name, f"{target.name}/index.html") for target in targets],
    )
    return targets


def main() -> int:
    args = parse_args()
    for target in restore_overlay(args.site_dir, args.cache_dir, args.image_list):
        print(f"restored YOLO dataset overlay at {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
