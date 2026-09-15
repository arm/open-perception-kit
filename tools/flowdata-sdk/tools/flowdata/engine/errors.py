################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
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
