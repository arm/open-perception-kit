################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Validate the caller-provided model before PEK loads it."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
from typing import Any

from support.runtime import ExampleError, ShutdownState


def ensure_model(model_path: Path, shutdown: ShutdownState) -> None:
    """Require the local model file and verify its declared digest."""

    descriptor = model_path.with_name("model.json")
    expected_sha256 = _expected_sha256(descriptor)
    shutdown.check()
    if not model_path.exists():
        raise ExampleError(
            f"model file is missing: {model_path}; provide the file declared by "
            "model.json before running the example"
        )
    if not model_path.is_file():
        raise ExampleError(f"model path is not a file: {model_path}")

    actual_sha256 = _sha256(model_path, shutdown)
    if actual_sha256 != expected_sha256:
        raise ExampleError(
            f"model SHA-256 mismatch: expected {expected_sha256}, got {actual_sha256}; "
            "replace the local model file before running the example"
        )
    print(f"Using verified model: {model_path}", file=sys.stderr)


def _expected_sha256(descriptor: Path) -> str:
    try:
        document: Any = json.loads(descriptor.read_text())
        expected_sha256 = document["hfDownload"]["sha256"]
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as exc:
        raise ExampleError(
            f"model descriptor does not provide hfDownload.sha256: {descriptor}"
        ) from exc
    if (
        not isinstance(expected_sha256, str)
        or len(expected_sha256) != 64
        or any(character not in "0123456789abcdef" for character in expected_sha256)
    ):
        raise ExampleError(f"invalid hfDownload.sha256 in {descriptor}")
    return expected_sha256


def _sha256(path: Path, shutdown: ShutdownState) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            shutdown.check()
            digest.update(chunk)
    shutdown.check()
    return digest.hexdigest()
