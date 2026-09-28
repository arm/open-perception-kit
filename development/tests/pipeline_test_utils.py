#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

"""Shared plumbing for Python pipeline integration tests."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def load_gstreamer_plugins(plugin_paths: list[Path]) -> Any:
    import gi

    gi.require_version("Gst", "1.0")
    from gi.repository import Gst

    Gst.init(None)
    for plugin_path in plugin_paths:
        Gst.Plugin.load_file(str(plugin_path))
    return Gst


def write_test_opchain(
    directory: str, name: str, attributes: dict[str, object]
) -> Path:
    descriptor = Path(directory) / f"opchain-{name}.json"
    descriptor.write_text(
        json.dumps(
            {
                "version": "1.0.0",
                "name": f"pipeline-test-{name}",
                "displayName": f"Pipeline Test {name}",
                "task": "Pipeline integration",
                "runtime": "Test",
                "description": "Pipeline integration test OpChain",
                "ops": [
                    {
                        "id": "opk-test-qos-delay/Delay",
                        "attributes": attributes,
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    return descriptor


def release_pipeline(pipeline: Any, gst: Any) -> None:
    if pipeline is None:
        return
    pipeline.set_state(gst.State.NULL)
