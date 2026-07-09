#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import time

from ..diagnostics import log_agent_diagnostic
from ..review.context import ReviewRunContext
from ..sdk_runtime import RunContextWrapper, function_tool


@function_tool
def get_review_context(wrapper: RunContextWrapper[ReviewRunContext]) -> dict[str, object]:
    """Return bounded review scope and basic untrusted pull request fields."""

    start = time.monotonic()
    payload = wrapper.context.model_payload()
    log_agent_diagnostic(
        "tool_call",
        tool="get_review_context",
        base_sha=wrapper.context.base_sha[:8],
        head_sha=wrapper.context.head_sha[:8],
        elapsed_ms=int((time.monotonic() - start) * 1000),
    )
    return payload
