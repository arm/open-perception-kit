################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Acquire and verify the model through the repository download owner."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

from support.runtime import (
    ExampleError,
    ExampleInterrupted,
    ShutdownState,
    run_managed_command,
)


def ensure_model(
    model_path: Path,
    repository_root: Path,
    shutdown: ShutdownState,
) -> None:
    """Reuse a verified model or materialize its descriptor's hfDownload."""

    descriptor = model_path.with_name("model.json")
    expected_sha256 = _expected_sha256(descriptor)
    shutdown.check()
    if model_path.is_file():
        existing_sha256 = _sha256(model_path, shutdown)
        if existing_sha256 == expected_sha256:
            print(f"Using verified cached model: {model_path}", file=sys.stderr)
            return
        print(
            f"Cached model has SHA-256 {existing_sha256}; requesting a verified replacement.",
            file=sys.stderr,
        )

    downloader = repository_root / "scripts/download-models.py"
    if not downloader.is_file():
        raise ExampleError(f"model download owner is missing: {downloader}")
    downloader_python = _model_downloader_python()

    status = run_managed_command(
        [downloader_python, str(downloader), "--models-dir", str(descriptor.parent)],
        shutdown,
        "model downloader",
    )
    if status == 130:
        raise ExampleInterrupted
    if status != 0:
        raise ExampleError(f"model download owner exited with status {status}")
    if not model_path.is_file():
        raise ExampleError(f"model download did not produce {model_path}")

    actual_sha256 = _sha256(model_path, shutdown)
    if actual_sha256 != expected_sha256:
        raise ExampleError(
            f"model SHA-256 mismatch: expected {expected_sha256}, got {actual_sha256}"
        )
    print(f"Downloaded and verified model: {model_path}", file=sys.stderr)


def _model_downloader_python() -> str:
    candidates = []
    if devtools_venv := os.environ.get("PEK_DEVTOOLS_VENV"):
        candidates.append(Path(devtools_venv).expanduser().resolve() / "bin/python")
    candidates.append(Path(sys.executable))

    for candidate in dict.fromkeys(candidates):
        if not candidate.is_file() or not os.access(candidate, os.X_OK):
            continue
        try:
            completed = subprocess.run(
                [str(candidate), "-c", "import huggingface_hub, jsonschema"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except OSError:
            continue
        if completed.returncode == 0:
            return str(candidate)
    raise ExampleError(
        "hfDownload requires the PEK devtools Python environment"
    )


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
