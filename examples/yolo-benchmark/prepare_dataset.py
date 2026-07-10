#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Prepare official COCO val2017 and write the benchmark image list."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import urllib.request
import zipfile
from pathlib import Path


COCO_BASE_URL = "https://s3.amazonaws.com/images.cocodataset.org"
FILES = {
    "val2017.zip": (
        f"{COCO_BASE_URL}/zips/val2017.zip",
        "4f7e2ccb2866ec5041993c9cf2a952bbed69647b115d0f74da7ce8f4bef82f05",  # pragma: allowlist secret
    ),
    "annotations_trainval2017.zip": (
        f"{COCO_BASE_URL}/annotations/annotations_trainval2017.zip",
        "113a836d90195ee1f884e704da6304dfaaecff1f023f49b6ca93c4aaae470268",  # pragma: allowlist secret
    ),
}
FINGERPRINT_HEADER = "# image_set_fingerprint="
DOWNLOAD_TIMEOUT_SECONDS = 60
READY_MARKER = ".coco-val2017-ready"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coco-dir", default="datasets/coco", type=Path)
    parser.add_argument("--output", default="artifacts/coco-val2017/images.tsv", type=Path)
    parser.add_argument("--limit", default=0, type=int)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256(path: Path, expected: str) -> None:
    actual = sha256_file(path)
    if actual != expected:
        raise ValueError(f"{path}: expected SHA256 {expected}, got {actual}")


def download(url: str, target: Path, expected_sha256: str) -> None:
    if target.is_file():
        try:
            verify_sha256(target, expected_sha256)
            print(f"using existing {target}")
            return
        except ValueError:
            print(f"discarding checksum-mismatched {target}")
            target.unlink()
    target.parent.mkdir(parents=True, exist_ok=True)
    print(f"downloading {url} -> {target}")
    with urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response, target.open("wb") as output:
        shutil.copyfileobj(response, output)
    verify_sha256(target, expected_sha256)


def safe_extract(zf: zipfile.ZipFile, target_dir: Path) -> None:
    root = target_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    for member in zf.infolist():
        target = (root / member.filename).resolve()
        try:
            target.relative_to(root)
        except ValueError as error:
            raise zipfile.BadZipFile(f"unsafe ZIP path: {member.filename}") from error
        if member.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        with zf.open(member) as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


def extract(archive: Path, target_dir: Path) -> None:
    print(f"extracting {archive} -> {target_dir}")
    with zipfile.ZipFile(archive) as zf:
        bad_file = zf.testzip()
        if bad_file is not None:
            raise zipfile.BadZipFile(f"{archive}: CRC check failed for {bad_file}")
        safe_extract(zf, target_dir)


def images_dir(coco_dir: Path) -> Path:
    for candidate in (coco_dir / "images" / "val2017", coco_dir / "val2017"):
        if candidate.is_dir():
            return candidate
    return coco_dir / "images" / "val2017"


def dataset_ready(coco_dir: Path) -> bool:
    return (
        (coco_dir / READY_MARKER).is_file()
        and (coco_dir / "annotations" / "instances_val2017.json").is_file()
        and images_dir(coco_dir).is_dir()
    )


def image_set_fingerprint(images: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for image in images:
        digest.update(f"{image['id']}\t{image['file_name']}\n".encode("utf-8"))
    return f"sha256:{digest.hexdigest()}"


def write_image_list(coco_dir: Path, output: Path, limit: int) -> None:
    annotations = coco_dir / "annotations" / "instances_val2017.json"
    with annotations.open("r", encoding="utf-8") as f:
        images = sorted(json.load(f)["images"], key=lambda image: image["id"])

    if limit > 0:
        images = images[:limit]

    image_root = images_dir(coco_dir)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as f:
        f.write(f"{FINGERPRINT_HEADER}{image_set_fingerprint(images)}\n")
        for image in images:
            path = image_root / image["file_name"]
            if not path.is_file():
                raise FileNotFoundError(path)
            f.write(f"{image['id']}\t{path.resolve()}\n")


def main() -> int:
    args = parse_args()
    download_dir = args.coco_dir / "downloads"

    if not dataset_ready(args.coco_dir):
        (args.coco_dir / READY_MARKER).unlink(missing_ok=True)
        for filename, (url, expected_sha256) in FILES.items():
            archive = download_dir / filename
            download(url, archive, expected_sha256)
            extract(archive, args.coco_dir)
        (args.coco_dir / READY_MARKER).write_text("ok\n", encoding="utf-8")

    write_image_list(args.coco_dir, args.output, args.limit)
    print(f"wrote COCO val2017 image list to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
