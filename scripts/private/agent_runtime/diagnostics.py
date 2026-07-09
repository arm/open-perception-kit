#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sys


MAX_DIAGNOSTIC_VALUE_CHARS = 180
AGENT_ACTION_LOG_ENV = "AGENT_ACTION_LOG"


def diagnostic_value(value: object) -> str:
    text = str(value).replace("\n", "\\n").replace("\r", "\\r")
    if len(text) > MAX_DIAGNOSTIC_VALUE_CHARS:
        return text[:MAX_DIAGNOSTIC_VALUE_CHARS] + "..."
    return text


def write_agent_action_log(timestamp: str, event: str, fields: dict[str, object]) -> None:
    path_value = os.environ.get(AGENT_ACTION_LOG_ENV)
    if not path_value:
        return
    path = Path(path_value)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as log_file:
            log_file.write(
                json.dumps(
                    {"ts": timestamp, "event": event, **fields},
                    ensure_ascii=False,
                    default=str,
                )
                + "\n"
            )
    except OSError as exc:
        print(f"agent-diagnostic-audit-log-error error={diagnostic_value(exc)}", file=sys.stderr, flush=True)


def log_agent_diagnostic(event: str, **fields: object) -> None:
    timestamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    write_agent_action_log(timestamp, event, fields)
    parts = [
        "agent-diagnostic",
        f"ts={timestamp}",
        f"event={event}",
    ]
    parts.extend(f"{name}={diagnostic_value(value)}" for name, value in fields.items())
    print(" ".join(parts), file=sys.stderr, flush=True)
