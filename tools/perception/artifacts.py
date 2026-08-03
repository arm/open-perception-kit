#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Acquire checksum-locked Perception SDK build artifacts."""

from __future__ import annotations

import shutil
import urllib.request
from pathlib import Path

from release_common import sha256
from sdk_config import LockedArtifact


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
        source = supplied.resolve()
        if source.name != artifact.filename:
            raise RuntimeError(
                f"expected {artifact.filename}, got {source.name}"
            )
        if not source.is_file():
            raise RuntimeError(f"missing locked artifact: {source}")
    elif cached is not None:
        source = cached
        if not source.is_file() or sha256(source) != artifact.sha256:
            temporary = source.with_suffix(source.suffix + ".tmp")
            if temporary.exists():
                temporary.unlink()
            try:
                urllib.request.urlretrieve(artifact.url, temporary)
                if sha256(temporary) != artifact.sha256:
                    raise RuntimeError(f"{artifact.filename} checksum mismatch after download")
                temporary.replace(source)
            finally:
                if temporary.exists():
                    temporary.unlink()
    else:
        source = destination
        if not destination.is_file() or sha256(destination) != artifact.sha256:
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
