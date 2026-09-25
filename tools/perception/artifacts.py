#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Acquire checksum-locked open-perception-kit build artifacts."""

from __future__ import annotations

import shutil
import urllib.request
from pathlib import Path

from release_common import sha256
from sdk_config import LockedArtifact


def _download_locked_artifact(artifact: LockedArtifact, destination: Path) -> None:
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    if temporary.exists():
        temporary.unlink()
    try:
        urllib.request.urlretrieve(artifact.url, temporary)
        if sha256(temporary) != artifact.sha256:
            raise RuntimeError(f"{artifact.filename} checksum mismatch after download")
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def _ensure_locked_artifact(artifact: LockedArtifact, path: Path) -> None:
    if not path.is_file() or sha256(path) != artifact.sha256:
        _download_locked_artifact(artifact, path)


def _validate_supplied_artifact(artifact: LockedArtifact, supplied: Path) -> Path:
    source = supplied.resolve()
    if source.name != artifact.filename:
        raise RuntimeError(f"expected {artifact.filename}, got {source.name}")
    if not source.is_file():
        raise RuntimeError(f"missing locked artifact: {source}")
    return source


def acquire_artifact(
    artifact: LockedArtifact,
    destination_dir: Path,
    supplied: Path | None = None,
    cache_dir: Path | None = None,
) -> Path:
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / artifact.filename
    cached = cache_dir.resolve() / artifact.filename if cache_dir is not None else None
    if cached is not None:
        cached.parent.mkdir(parents=True, exist_ok=True)
    if supplied is not None:
        source = _validate_supplied_artifact(artifact, supplied)
    elif cached is not None:
        source = cached
        _ensure_locked_artifact(artifact, source)
    else:
        source = destination
        _ensure_locked_artifact(artifact, destination)
    if source != destination:
        if sha256(source) != artifact.sha256:
            raise RuntimeError(f"{artifact.filename} checksum mismatch")
        shutil.copy2(source, destination)
    if not destination.is_file():
        raise RuntimeError(f"missing locked artifact: {destination}")
    actual = sha256(destination)
    if actual != artifact.sha256:
        raise RuntimeError(
            f"{artifact.filename} checksum mismatch: expected "
            f"{artifact.sha256}, got {actual}"
        )
    return destination
