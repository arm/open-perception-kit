#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from datetime import datetime, timezone
import sys


MAX_DIAGNOSTIC_VALUE_CHARS = 180


def diagnostic_value(value: object) -> str:
    text = str(value).replace("\n", "\\n").replace("\r", "\\r")
    if len(text) > MAX_DIAGNOSTIC_VALUE_CHARS:
        return text[:MAX_DIAGNOSTIC_VALUE_CHARS] + "..."
    return text


def log_agent_diagnostic(event: str, **fields: object) -> None:
    parts = [
        "agent-diagnostic",
        f"ts={datetime.now(timezone.utc).isoformat(timespec='seconds')}",
        f"event={event}",
    ]
    parts.extend(f"{name}={diagnostic_value(value)}" for name, value in fields.items())
    print(" ".join(parts), file=sys.stderr, flush=True)
