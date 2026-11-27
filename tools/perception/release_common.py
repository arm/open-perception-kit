#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

"""Shared deterministic release-tool primitives."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from pathlib import Path, PurePosixPath


def command_output(command: list[str]) -> str:
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    return result.stdout.strip() or result.stderr.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_typescript_runtime(version: str, runtimes: object) -> None:
    """Check the pinned npm runtime against FlowData's declared dynamic range."""
    records = [entry for entry in runtimes if isinstance(entry, dict)
               and entry.get("language") == "typescript"
               and entry.get("package") == "flatbuffers"] if isinstance(runtimes, list) else []
    requirement = records[0].get("version_requirement") if len(records) == 1 else None
    match = re.fullmatch(r">=(\d+\.\d+\.\d+),<(\d+\.\d+\.\d+)",
                         requirement if isinstance(requirement, str) else "")
    if match is None:
        raise RuntimeError("missing or unsupported TypeScript FlatBuffers runtime contract")
    if not isinstance(version, str) or re.fullmatch(r"\d+\.\d+\.\d+", version) is None:
        raise RuntimeError("invalid TypeScript FlatBuffers runtime version")
    lower, upper = (tuple(map(int, bound.split("."))) for bound in match.groups())
    if not lower <= tuple(map(int, version.split("."))) < upper:
        raise RuntimeError(f"TypeScript FlatBuffers {version} does not satisfy {requirement}")


def load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"expected a JSON object: {path}")
    return value


def validate_relative_path(value: object) -> Path:
    if not isinstance(value, str) or not value:
        raise RuntimeError("manifest path must be a non-empty string")
    if "\\" in value:
        raise RuntimeError(f"manifest path must use POSIX separators: {value}")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise RuntimeError(f"unsafe manifest path: {value}")
    return Path(*path.parts)
