#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
