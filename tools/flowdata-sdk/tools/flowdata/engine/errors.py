################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import re
from pathlib import Path
from typing import NoReturn


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def require_match(
    pattern: str,
    text: str,
    path: Path,
    message: str,
    *,
    flags: int = 0,
) -> re.Match[str]:
    match = re.search(pattern, text, flags=flags)
    if not match:
        fail(f"{path}: {message}")
    return match
