#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import json
from pathlib import Path


def write_json_file(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
